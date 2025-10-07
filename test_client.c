#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 4096

void print_usage() {
    printf("Simple DropBox Client\n");
    printf("Commands:\n");
    printf("  LOGIN <username> <password>     - Login to existing account\n");
    printf("  SIGNUP <username> <password>    - Create new account\n");
    printf("  UPLOAD <filename>              - Upload a file\n");
    printf("  DOWNLOAD <filename>            - Download a file\n");
    printf("  DELETE <filename>              - Delete a file\n");
    printf("  LIST                           - List all files\n");
    printf("  QUIT                           - Exit client\n");
    printf("  HELP                           - Show this help\n");
}

static void trim(char *s){
    if(!s) return; size_t len=strlen(s); while(len>0 && (s[len-1]==' '||s[len-1]=='\t')) s[--len]='\0';
    char *p=s; while(*p==' '||*p=='\t') ++p; if(p!=s) memmove(s,p,strlen(p)+1);
}

int main() {
    int client_socket;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    
    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Failed to create socket");
        return EXIT_FAILURE;
    }
    
    // Set up server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    // Connect to server
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to connect to server");
        close(client_socket);
        return EXIT_FAILURE;
    }
    
    printf("Connected to DropBox Server at 127.0.0.1:%d\n", PORT);
    print_usage();
    printf("\n");
    setvbuf(stdout,NULL,_IOLBF,0);
    int pending_upload = 0;
    char pending_filename[BUFFER_SIZE] = {0};
    // Communication loop
    while (1) {
        // Receive server response
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        printf("[CLIENT] recv returned: %zd\n", received);
        if (received <= 0) {
            printf("Connection closed by server\n");
            break;
        }
        printf("%s", buffer);
        // If SEND_FILE_DATA is received and upload is pending, perform upload
        if (strstr(buffer, "SEND_FILE_DATA") && !pending_upload) {
            printf("[CLIENT][WARN] Received SEND_FILE_DATA but no pending upload flag set. Buffer='%s'\n", buffer);
        }
        if (pending_upload && strstr(buffer, "SEND_FILE_DATA")) {
            printf("[CLIENT] Entering upload block\n");
            FILE *fp = fopen(pending_filename, "rb");
            if (!fp) {
                printf("Failed to open file: %s\n", pending_filename);
                pending_upload = 0;
                continue;
            }
            fseek(fp, 0, SEEK_END);
            size_t file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            printf("[CLIENT] Sending file size: %zu\n", file_size);
            unsigned char *size_bytes = (unsigned char*)&file_size;
            printf("[CLIENT] File size bytes: ");
            for (size_t i = 0; i < sizeof(size_t); ++i) printf("%02x ", size_bytes[i]);
            printf("\n");
            ssize_t sent_size = send(client_socket, &file_size, sizeof(size_t), 0);
            printf("[CLIENT] send(file_size) returned: %zd\n", sent_size);
            if (sent_size != sizeof(size_t)) {
                printf("Failed to send file size\n");
                fclose(fp);
                pending_upload = 0;
                continue;
            }
            char file_buf[BUFFER_SIZE];
            size_t sent = 0;
            while (sent < file_size) {
                size_t to_read = (file_size - sent > BUFFER_SIZE) ? BUFFER_SIZE : (file_size - sent);
                size_t read_bytes = fread(file_buf, 1, to_read, fp);
                if (read_bytes == 0) break;
                ssize_t sent_bytes = send(client_socket, file_buf, read_bytes, 0);
                printf("[CLIENT] send(file_data) returned: %zd\n", sent_bytes);
                if (sent_bytes != read_bytes) {
                    printf("Failed to send file data\n");
                    break;
                }
                sent += read_bytes;
            }
            fclose(fp);
            printf("File upload complete (%zu bytes sent)\n", sent);
            pending_upload = 0;
            continue;
        }
        // Check if server is asking for input
        if (strstr(buffer, "> ") || strstr(buffer, ": ")) {
            // Get user input
            char input[BUFFER_SIZE];
            if (fgets(input, sizeof(input), stdin) == NULL) {
                break;
            }
            // Remove trailing newline
            char *newline = strchr(input, '\n');
            if (newline) *newline = '\0';
            // Handle local commands
            if (strcasecmp(input, "HELP") == 0) {
                print_usage();
                continue;
            }
            if (strcasecmp(input, "QUIT") == 0) {
                send(client_socket, input, strlen(input), 0);
                break;
            }
            // Handle UPLOAD command
            if (strncasecmp(input, "UPLOAD ", 7) == 0) {
                trim(input);
                if (send(client_socket, input, strlen(input), 0) < 0) {
                    perror("Failed to send command");
                    break;
                }
                strncpy(pending_filename, input + 7, BUFFER_SIZE - 1);
                trim(pending_filename);
                printf("[CLIENT] Pending upload set for '%s'\n", pending_filename);
                pending_upload = 1;
                continue;
            }

            // Send command to server
            if (send(client_socket, input, strlen(input), 0) < 0) {
                perror("Failed to send command");
                break;
            }
        }
    }
    
    close(client_socket);
    printf("Client disconnected\n");
    return EXIT_SUCCESS;
}
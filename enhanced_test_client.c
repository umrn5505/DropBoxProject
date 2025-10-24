#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_FILENAME 256

void send_file_data(int socket_fd, const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        printf("Error: Could not open file %s\n", filename);
        return;
    }
    
    
    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    printf("Uploading file %s (%zu bytes)...\n", filename, file_size);
    
    
    send(socket_fd, &file_size, sizeof(size_t), 0);
    
    
    char buffer[BUFFER_SIZE];
    size_t total_sent = 0;
    
    while (total_sent < file_size) {
        size_t to_read = (file_size - total_sent > BUFFER_SIZE) ? BUFFER_SIZE : (file_size - total_sent);
        size_t read_size = fread(buffer, 1, to_read, file);
        
        if (read_size == 0) break;
        
        ssize_t sent = send(socket_fd, buffer, read_size, 0);
        if (sent <= 0) {
            printf("Error sending file data\n");
            break;
        }
        
        total_sent += sent;
        printf("Sent %zu/%zu bytes\r", total_sent, file_size);
        fflush(stdout);
    }
    
    printf("\nFile upload completed.\n");
    fclose(file);
}

void receive_file_data(int socket_fd, const char *filename) {
    
    size_t file_size;
    if (recv(socket_fd, &file_size, sizeof(size_t), 0) != sizeof(size_t)) {
        printf("Error receiving file size\n");
        return;
    }
    
    printf("Downloading file %s (%zu bytes)...\n", filename, file_size);
    
    FILE *file = fopen(filename, "wb");
    if (!file) {
        printf("Error: Could not create file %s\n", filename);
        return;
    }
    
    
    char buffer[BUFFER_SIZE];
    size_t total_received = 0;
    
    while (total_received < file_size) {
        size_t to_receive = (file_size - total_received > BUFFER_SIZE) ? BUFFER_SIZE : (file_size - total_received);
        ssize_t received = recv(socket_fd, buffer, to_receive, 0);
        
        if (received <= 0) {
            printf("Error receiving file data\n");
            break;
        }
        
        fwrite(buffer, 1, received, file);
        total_received += received;
        printf("Received %zu/%zu bytes\r", total_received, file_size);
        fflush(stdout);
    }
    
    printf("\nFile download completed.\n");
    fclose(file);
}

int main() {
    int socket_fd;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
    char command[256];
    char filename[MAX_FILENAME];
    
    
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }
    
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    
    if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(socket_fd);
        return 1;
    }
    
    printf("Connected to DropBox server!\n");
    
    
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
        printf("%s", buffer);
        
        if (strstr(buffer, "Available commands:")) {
            break; 
        }
        
        
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0; 
        
        send(socket_fd, command, strlen(command), 0);
        
       
        memset(buffer, 0, BUFFER_SIZE);
        recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
        printf("%s", buffer);
        
        if (strstr(buffer, "SUCCESS")) {
            
            memset(buffer, 0, BUFFER_SIZE);
            recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
            printf("%s", buffer);
            break;
        }
    }
    
    printf("\n=== Enhanced DropBox Client with Priority Support ===\n");
    printf("Available commands:\n");
    printf("  UPLOAD <filename> [--priority=high|medium|low]\n");
    printf("  DOWNLOAD <filename> [--priority=high|medium|low]\n");
    printf("  DELETE <filename> [--priority=high|medium|low]\n");
    printf("  LIST [--priority=high|medium|low]\n");
    printf("  QUIT\n\n");
    
    while (1) {
        printf("> ");
        fflush(stdout);
        
        
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0; 
        
        if (strlen(command) == 0) continue;
        
        
        if (strcmp(command, "QUIT") == 0 || strcmp(command, "quit") == 0) {
            send(socket_fd, command, strlen(command), 0);
            break;
        }
        
       
        send(socket_fd, command, strlen(command), 0);
        
        
        char cmd_type[64];
        sscanf(command, "%63s %255s", cmd_type, filename);
        
        
        for (int i = 0; cmd_type[i]; i++) {
            cmd_type[i] = toupper(cmd_type[i]);
        }
        
        
        memset(buffer, 0, BUFFER_SIZE);
        recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
        
        if (strcmp(cmd_type, "UPLOAD") == 0) {
            if (strstr(buffer, "SEND_FILE_DATA")) {
                send_file_data(socket_fd, filename);
                
                memset(buffer, 0, BUFFER_SIZE);
                recv(socket_fd, buffer, BUFFER_SIZE - 1, 0);
            }
        } else if (strcmp(cmd_type, "DOWNLOAD") == 0) {
            if (strstr(buffer, "SUCCESS") || buffer[0] == 0) {
                
                receive_file_data(socket_fd, filename);
                continue; 
            }
        }
        
        printf("%s", buffer);
    }
    
    close(socket_fd);
    printf("Disconnected from server.\n");
    return 0;
}
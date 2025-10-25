// language: c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <strings.h> // for strcasecmp/strncasecmp

#ifndef PORT
#define PORT 8080
#endif
#define BUFFER_SIZE 4096

void print_usage() {
    printf("Simple DropBox Client\n");
    printf("Commands:\n");
    printf("  LOGIN <username> <password>    - Login to existing account\n");
    printf("  SIGNUP <username> <password>   - Create new account\n");
    printf("  UPLOAD <filename>              - Upload a file\n");
    printf("  DOWNLOAD <filename>            - Download a file\n");
    printf("  DELETE <filename>              - Delete a file\n");
    printf("  LIST                           - List all files\n");
    printf("  QUIT                           - Exit client\n");
    printf("  HELP                           - Show this help\n");
}

static void trim(char *s){
    if(!s) return;
    size_t len = strlen(s);
    while(len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
    char *p = s;
    while(*p == ' ' || *p == '\t') ++p;
    if(p != s) memmove(s, p, strlen(p)+1);
}

static void show_prompt(){ printf("> "); fflush(stdout); }

static int prompt_at_end(const char *buf) {
    if(!buf) return 0;
    size_t len = strlen(buf);
    while(len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r')) len--;
    if(len < 2) return 0;
    char a = buf[len-2], b = buf[len-1];
    return ((a==':' && b==' ') || (a=='>' && b==' '));
}

/* Check if a file exists and is readable */
static int file_exists_readable(const char *path){
    if(!path || !*path) return 0;
    FILE *f = fopen(path, "rb");
    if(!f) return 0;
    fclose(f);
    return 1;
}

int main(int argc, char **argv) {
    const char *host = "127.0.0.1";
    int port = PORT;

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = atoi(argv[2]);

    int client_socket = -1;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];

    // Create socket
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        perror("Failed to create socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_addr.sin_addr) <= 0) {
        // try resolving via gethostbyname
        struct hostent *he = gethostbyname(host);
        if (!he) {
            fprintf(stderr, "Invalid host: %s\n", host);
            close(client_socket);
            return EXIT_FAILURE;
        }
        memcpy(&server_addr.sin_addr, he->h_addr_list[0], he->h_length);
    }

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to connect to server");
        close(client_socket);
        return EXIT_FAILURE;
    }

    printf("Connected to DropBox Server at %s:%d\n", host, port);
    print_usage();
    printf("\n");
    setvbuf(stdout, NULL, _IOLBF, 0);

    int pending_upload = 0;
    int awaiting_file_data = 0;
    int authenticated = 0;
    int waiting_response = 1;
    char pending_filename[BUFFER_SIZE] = {0};

    // Communication loop
    while (1) {
        if (waiting_response) {
            memset(buffer, 0, BUFFER_SIZE);
            ssize_t received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
            if (received <= 0) { printf("Connection closed by server\n"); break; }
            buffer[received] = '\0';

            // Highlight SUCCESS/ERROR lines
            if (strstr(buffer, "SUCCESS:") || strstr(buffer, "ERROR:")) {
                char tmpbuf[BUFFER_SIZE];
                strncpy(tmpbuf, buffer, BUFFER_SIZE-1);
                tmpbuf[BUFFER_SIZE-1] = '\0';
                char *saveptr = NULL;
                char *line = strtok_r(tmpbuf, "\n", &saveptr);
                while (line) {
                    if (strstr(line, "SUCCESS:") || strstr(line, "ERROR:")) {
                        // uppercase copy
                        char dup[BUFFER_SIZE];
                        size_t i; for (i=0; i<strlen(line) && i < sizeof(dup)-1; ++i) dup[i] = (char)toupper((unsigned char)line[i]); dup[i] = '\0';
                        printf("\033[1;32m==== %s ====\033[0m\n", dup);
                    } else {
                        printf("%s\n", line);
                    }
                    line = strtok_r(NULL, "\n", &saveptr);
                }
            } else {
                printf("%s", buffer);
            }

            if (strstr(buffer, "LOGIN_SUCCESS") || strstr(buffer, "SIGNUP_SUCCESS") || strstr(buffer, "Authenticated successfully.")) authenticated = 1;
            if (strstr(buffer, "SEND_FILE_DATA")) { awaiting_file_data = 1; }

            if (awaiting_file_data && pending_upload) {
                FILE *fp = fopen(pending_filename, "rb");
                if (!fp) {
                    printf("Error: Local file '%s' no longer accessible. Upload cancelled.\n", pending_filename);
                    size_t zero = 0;
                    send(client_socket, (const char*)&zero, sizeof(size_t), 0);
                } else {
                    fseek(fp, 0, SEEK_END);
                    size_t file_size = ftell(fp);
                    fseek(fp, 0, SEEK_SET);
                    // send file size
                    if (send(client_socket, (const char*)&file_size, sizeof(size_t), 0) != sizeof(size_t)) {
                        perror("send(file_size)");
                    }

                    size_t sent = 0;
                    char fb[BUFFER_SIZE];
                    while (sent < file_size) {
                        size_t to_read = (file_size - sent > BUFFER_SIZE) ? BUFFER_SIZE : (file_size - sent);
                        size_t r = fread(fb, 1, to_read, fp);
                        if (!r) break;
                        ssize_t s = send(client_socket, fb, (int)r, 0);
                        if (s <= 0) { perror("send"); break; }
                        sent += (size_t)s;
                    }
                    fclose(fp);
                    printf("File upload complete (%zu bytes sent)\n", sent);
                }
                pending_upload = 0;
                awaiting_file_data = 0;
            }

            if (prompt_at_end(buffer) && !awaiting_file_data) {
                waiting_response = 0;
            }
            continue;
        }

        // Input phase
        show_prompt();
        char input[BUFFER_SIZE];
        if (!fgets(input, sizeof(input), stdin)) break;
        char *nl = strchr(input, '\n'); if (nl) *nl = '\0';
        trim(input);
        if (strlen(input) == 0) { continue; }

        if (!authenticated) {
            send(client_socket, input, (int)strlen(input), 0);
            waiting_response = 1;
            continue;
        }

        if (strcasecmp(input, "HELP") == 0) { print_usage(); continue; }
        if (strcasecmp(input, "QUIT") == 0) { send(client_socket, input, (int)strlen(input), 0); break; }

        if (strncasecmp(input, "UPLOAD ", 7) == 0) {
            const char *fname = input + 7;
            while (*fname == ' ' || *fname == '\t') ++fname;
            if (strlen(fname) == 0) { printf("Error: Missing filename for upload.\n"); continue; }
            if (!file_exists_readable(fname)) { printf("Error: File '%s' not found or unreadable.\n", fname); continue; }
            strncpy(pending_filename, fname, BUFFER_SIZE-1);
            pending_filename[BUFFER_SIZE-1] = '\0';
            pending_upload = 1;
            send(client_socket, input, (int)strlen(input), 0);
            waiting_response = 1;
            continue;
        }

        send(client_socket, input, (int)strlen(input), 0);
        waiting_response = 1;
    }

    close(client_socket);
    printf("Client disconnected\n");
    return EXIT_SUCCESS;
}

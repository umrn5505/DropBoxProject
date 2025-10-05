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
    
    // Communication loop
    while (1) {
        // Receive server response
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t received = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (received <= 0) {
            printf("Connection closed by server\n");
            break;
        }
        
        printf("%s", buffer);
        
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
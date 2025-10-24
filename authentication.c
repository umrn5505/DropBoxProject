#include "dropbox_server.h"

// Authentication functions
int authenticate_user(int socket_fd, char *username) {
    char buffer[BUFFER_SIZE];
    char command[64], user[MAX_USERNAME], pass[MAX_PASSWORD];
    
    // Send welcome message
    send_response(socket_fd, "Welcome to DropBox Server!\n");
    send_response(socket_fd, "Please login or signup (LOGIN <username> <password> or SIGNUP <username> <password>): ");
    
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        if (receive_data(socket_fd, buffer, BUFFER_SIZE) <= 0) {
            return -1; // Connection closed
        }
        
        // Parse authentication command
        int parsed = sscanf(buffer, "%63s %49s %49s", command, user, pass);
        if (parsed != 3) {
            send_response(socket_fd, "ERROR: Invalid command format. Use LOGIN <username> <password> or SIGNUP <username> <password>\n");
            continue;
        }
        
        // Convert command to uppercase for case-insensitive comparison
        for (int i = 0; command[i]; i++) {
            command[i] = toupper(command[i]);
        }
        
        if (strcmp(command, "LOGIN") == 0) {
            if (handle_login(socket_fd, user, pass) == 0) {
                strncpy(username, user, MAX_USERNAME - 1);
                username[MAX_USERNAME - 1] = '\0';
                send_response(socket_fd, "LOGIN_SUCCESS: Authentication successful\n");
                printf("User '%s' logged in successfully on socket %d\n", username, socket_fd);
                return 0; // Success
            } else {
                send_response(socket_fd, "LOGIN_FAILED: Invalid username or password\n");
            }
        } else if (strcmp(command, "SIGNUP") == 0) {
            if (handle_signup(socket_fd, user, pass) == 0) {
                strncpy(username, user, MAX_USERNAME - 1);
                username[MAX_USERNAME - 1] = '\0';
                send_response(socket_fd, "SIGNUP_SUCCESS: Account created and logged in\n");
                printf("User '%s' signed up and logged in successfully on socket %d\n", username, socket_fd);
                return 0; // Success
            } else {
                send_response(socket_fd, "SIGNUP_FAILED: Username already exists or invalid credentials\n");
            }
        } else {
            send_response(socket_fd, "ERROR: Unknown command. Use LOGIN or SIGNUP\n");
        }
    }
}

int handle_signup(int socket_fd, const char *username, const char *password) {
    (void)socket_fd; // Unused parameter
    
    // Basic validation
    if (!username || !password || strlen(username) == 0 || strlen(password) == 0) {
        return -1;
    }
    
    if (strlen(username) >= MAX_USERNAME || strlen(password) >= MAX_PASSWORD) {
        return -1;
    }
    
    // Create users directory if it doesn't exist
    struct stat st = {0};
    if (stat("users", &st) == -1) {
        if (mkdir("users", 0700) != 0) {
            perror("Failed to create users directory");
            return -1;
        }
    }
    
    // Check if user already exists
    char user_file[512];
    snprintf(user_file, sizeof(user_file), "users/%s.txt", username);
    
    FILE *file = fopen(user_file, "r");
    if (file != NULL) {
        fclose(file);
        return -1; // User already exists
    }
    
    // Create user file with password (in a real system, you'd hash the password)
    file = fopen(user_file, "w");
    if (!file) {
        perror("Failed to create user file");
        return -1;
    }
    
    fprintf(file, "%s\n", password);
    fclose(file);
    
    // Create user directory for file storage
    char user_dir[512];
    snprintf(user_dir, sizeof(user_dir), "storage/%s", username);
    if (stat("storage", &st) == -1) {
        if (mkdir("storage", 0700) != 0) {
            perror("Failed to create storage directory");
            return -1;
        }
    }
    
    if (mkdir(user_dir, 0700) != 0) {
        perror("Failed to create user storage directory");
        return -1;
    }
    
    printf("User '%s' created successfully\n", username);
    return 0;
}

int handle_login(int socket_fd, const char *username, const char *password) {
    (void)socket_fd; // Unused parameter
    
    // Basic validation
    if (!username || !password || strlen(username) == 0 || strlen(password) == 0) {
        return -1;
    }
    
    char user_file[512];
    snprintf(user_file, sizeof(user_file), "users/%s.txt", username);
    
    FILE *file = fopen(user_file, "r");
    if (!file) {
        return -1; // User doesn't exist
    }
    
    char stored_password[MAX_PASSWORD];
    if (fgets(stored_password, sizeof(stored_password), file) == NULL) {
        fclose(file);
        return -1;
    }
    fclose(file);
    
    // Remove newline from stored password
    char *newline = strchr(stored_password, '\n');
    if (newline) *newline = '\0';
    
    // Compare passwords
    if (strcmp(password, stored_password) == 0) {
        printf("User '%s' authentication successful\n", username);
        return 0;
    }
    
    printf("User '%s' authentication failed\n", username);
    return -1;
}

int parse_command(const char *command_line, char *command, char *filename) {
    if (!command_line || !command || !filename) {
        return -1;
    }
    
    // Initialize output parameters
    command[0] = '\0';
    filename[0] = '\0';
    
    // Skip leading whitespace
    while (*command_line && isspace(*command_line)) {
        command_line++;
    }
    
    // Parse command
    int parsed = sscanf(command_line, "%255s %255s", command, filename);
    
    if (parsed < 1) {
        return -1; // No command found
    }
    
    // Convert command to uppercase for case-insensitive comparison
    for (int i = 0; command[i]; i++) {
        command[i] = toupper(command[i]);
    }
    
    // Validate command
    if (strcmp(command, "UPLOAD") == 0 || 
        strcmp(command, "DOWNLOAD") == 0 || 
        strcmp(command, "DELETE") == 0) {
        if (parsed < 2 || strlen(filename) == 0) {
            return -1; // These commands require a filename
        }
    } else if (strcmp(command, "LIST") == 0) {
        // LIST doesn't require a filename
        filename[0] = '\0';
    } else if (strcmp(command, "QUIT") == 0 || strcmp(command, "EXIT") == 0) {
        // Quit commands
        filename[0] = '\0';
    } else {
        return -1; // Unknown command
    }
    
    return 0;
}

// Enhanced command parsing with priority support
int parse_priority_command(const char *command_line, char *command, char *filename, int *priority) {
    if (!command_line || !command || !filename || !priority) return -1;
    
    // Initialize outputs
    command[0] = '\0';
    filename[0] = '\0';
    *priority = PRIORITY_MEDIUM; // Default priority
    
    // Parse command with optional priority flag
    // Format: COMMAND [filename] [--priority=high|medium|low] or [--high|--medium|--low]
    char temp_command[64] = {0};
    char temp_filename[MAX_FILENAME] = {0};
    char priority_flag[32] = {0};
    
    int parsed = sscanf(command_line, "%63s %255s %31s", temp_command, temp_filename, priority_flag);
    
    if (parsed < 1) return -1;
    
    // Convert command to uppercase
    for (int i = 0; temp_command[i]; i++) {
        temp_command[i] = toupper(temp_command[i]);
    }
    
    // Check if second argument is a priority flag instead of filename
    if (parsed >= 2 && (strncmp(temp_filename, "--", 2) == 0 || temp_filename[0] == '-')) {
        // Priority flag in place of filename, move it to priority_flag
        strcpy(priority_flag, temp_filename);
        temp_filename[0] = '\0';
        parsed = 2; // Adjust parsed count
    }
    
    // Parse priority flag if present
    if (strlen(priority_flag) > 0) {
        if (strcmp(priority_flag, "--high") == 0 || strcmp(priority_flag, "--priority=high") == 0) {
            *priority = PRIORITY_HIGH;
        } else if (strcmp(priority_flag, "--medium") == 0 || strcmp(priority_flag, "--priority=medium") == 0) {
            *priority = PRIORITY_MEDIUM;
        } else if (strcmp(priority_flag, "--low") == 0 || strcmp(priority_flag, "--priority=low") == 0) {
            *priority = PRIORITY_LOW;
        }
        // If unrecognized priority flag, keep default medium priority
    }
    
    // Validate command and filename requirements
    if (strcmp(temp_command, "UPLOAD") == 0 || 
        strcmp(temp_command, "DOWNLOAD") == 0 || 
        strcmp(temp_command, "DELETE") == 0) {
        if (strlen(temp_filename) == 0) {
            return -1; // These commands require a filename
        }
        strcpy(filename, temp_filename);
    } else if (strcmp(temp_command, "LIST") == 0) {
        // LIST doesn't require a filename
        filename[0] = '\0';
    } else if (strcmp(temp_command, "QUIT") == 0 || strcmp(temp_command, "EXIT") == 0) {
        // Quit commands
        filename[0] = '\0';
    } else {
        return -1; // Unknown command
    }
    
    strcpy(command, temp_command);
    return 0;
}
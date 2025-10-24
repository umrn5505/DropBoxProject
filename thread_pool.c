#include "dropbox_server.h"
#include <unistd.h>

// Client thread function - handles authentication and command parsing
void* client_thread_function(void *arg) {
    server_context_t *server = (server_context_t *)arg;
    char username[MAX_USERNAME];
    char buffer[BUFFER_SIZE];
    char command[256], filename[MAX_FILENAME];
    
    printf("Client thread %lu started\n", pthread_self());
    
    while (1) {
        // Check for shutdown signal
        pthread_mutex_lock(&server->shutdown_mutex);
        int shutdown = server->shutdown_flag;
        pthread_mutex_unlock(&server->shutdown_mutex);
        
        if (shutdown) {
            printf("Client thread %lu shutting down\n", pthread_self());
            break;
        }
        
        // Get a client socket from the queue
        int client_socket = dequeue_client(server->client_queue);
        if (client_socket < 0) {
            continue; // This might happen during shutdown
        }
        
        printf("Client thread %lu handling socket %d\n", pthread_self(), client_socket);
        
        // Perform authentication
        if (authenticate_user(client_socket, username) != 0) {
            printf("Authentication failed for socket %d\n", client_socket);
            close(client_socket);
            continue;
        }
        
        // Send command prompt
        send_response(client_socket, "Authenticated successfully. Available commands: UPLOAD <filename>, DOWNLOAD <filename>, DELETE <filename>, LIST, QUIT\n");
        send_response(client_socket, "> ");
        
        // Command processing loop
        while (1) {
            // Check for shutdown signal
            pthread_mutex_lock(&server->shutdown_mutex);
            shutdown = server->shutdown_flag;
            pthread_mutex_unlock(&server->shutdown_mutex);
            
            if (shutdown) {
                send_response(client_socket, "Server is shutting down. Goodbye!\n");
                break;
            }
            
            memset(buffer, 0, BUFFER_SIZE);
            if (receive_data(client_socket, buffer, BUFFER_SIZE) <= 0) {
                printf("Client disconnected (socket %d, user: %s)\n", client_socket, username);
                break;
            }
            
            // Remove trailing newline/carriage return
            char *newline = strchr(buffer, '\n');
            if (newline) *newline = '\0';
            newline = strchr(buffer, '\r');
            if (newline) *newline = '\0';
            
            printf("Received command from %s (socket %d): %s\n", username, client_socket, buffer);
            
            // Parse the command with priority support
            int priority = PRIORITY_MEDIUM;
            if (parse_priority_command(buffer, command, filename, &priority) != 0) {
                send_response(client_socket, "ERROR: Invalid command. Use UPLOAD <filename> [--priority=high|medium|low], DOWNLOAD <filename> [--priority=high|medium|low], DELETE <filename> [--priority=high|medium|low], LIST [--priority=high|medium|low], or QUIT\n> ");
                continue;
            }
            
            // Handle QUIT command locally
            if (strcmp(command, "QUIT") == 0 || strcmp(command, "EXIT") == 0) {
                send_response(client_socket, "Goodbye!\n");
                printf("User %s (socket %d) quit\n", username, client_socket);
                break;
            }
            
            // Create task for worker threads
            task_type_t task_type;
            if (strcmp(command, "UPLOAD") == 0) {
                task_type = TASK_UPLOAD;
            } else if (strcmp(command, "DOWNLOAD") == 0) {
                task_type = TASK_DOWNLOAD;
            } else if (strcmp(command, "DELETE") == 0) {
                task_type = TASK_DELETE;
            } else if (strcmp(command, "LIST") == 0) {
                task_type = TASK_LIST;
            } else {
                send_response(client_socket, "ERROR: Unknown command\n> ");
                continue;
            }
            
            // Create and submit priority task to worker thread pool
            task_t *task = create_priority_task(task_type, client_socket, username, buffer, priority);
            if (!task) {
                send_response(client_socket, "ERROR: Failed to create task\n> ");
                continue;
            }
            
            // Copy filename to task
            strncpy(task->filename, filename, MAX_FILENAME - 1);
            task->filename[MAX_FILENAME - 1] = '\0';
            
            // Enqueue task with priority and wait for completion
            if (enqueue_priority_task(server->task_queue, task) != 0) {
                send_response(client_socket, "ERROR: Failed to enqueue task\n> ");
                destroy_task(task);
                continue;
            }
            
            printf("Priority task submitted by %s (socket %d, priority %d), waiting for completion...\n", 
                   username, client_socket, priority);
            
            // Wait for task completion using condition variable
            pthread_mutex_lock(&task->task_mutex);
            while (task->status == TASK_PENDING || task->status == TASK_IN_PROGRESS) {
                pthread_cond_wait(&task->task_cond, &task->task_mutex);
            }
            
            // Process task result
            if (task->status == TASK_COMPLETED) {
                if (task->result_code == 0) {
                    // Success - send result data if available
                    if (task->result_data && task->result_size > 0) {
                        send(client_socket, task->result_data, task->result_size, 0);
                    } else {
                        send_response(client_socket, "SUCCESS: Operation completed successfully\n");
                    }
                } else {
                    // Error - send error message
                    char error_response[BUFFER_SIZE];
                    const char *error_msg = strlen(task->error_message) > 0 ? task->error_message : "Unknown error";
                    snprintf(error_response, sizeof(error_response), "ERROR: %.4080s\n", error_msg);
                    send_response(client_socket, error_response);
                }
            } else {
                send_response(client_socket, "ERROR: Task failed to complete\n");
            }
            
            pthread_mutex_unlock(&task->task_mutex);
            
            // Clean up task
            destroy_task(task);
            
            // Send prompt for next command
            send_response(client_socket, "> ");
        }
        
        // Close client socket
        close(client_socket);
        printf("Client thread %lu finished handling socket %d\n", pthread_self(), client_socket);
    }
    
    printf("Client thread %lu exiting\n", pthread_self());
    return NULL;
}

// Worker thread function - processes tasks from the task queue
void* worker_thread_function(void *arg) {
    server_context_t *server = (server_context_t *)arg;
    
    printf("Worker thread %lu started\n", pthread_self());
    
    while (1) {
        // Check for shutdown signal
        pthread_mutex_lock(&server->shutdown_mutex);
        int shutdown = server->shutdown_flag;
        pthread_mutex_unlock(&server->shutdown_mutex);
        
        if (shutdown) {
            printf("Worker thread %lu shutting down\n", pthread_self());
            break;
        }
        
        // Get a task from the queue
        task_t *task = dequeue_task(server->task_queue);
        if (!task) {
            continue; // This might happen during shutdown
        }
        
        printf("Worker thread %lu processing task type %d for user %s\n", 
               pthread_self(), task->type, task->username);
        
        // Update task status to in progress
        pthread_mutex_lock(&task->task_mutex);
        task->status = TASK_IN_PROGRESS;
        pthread_mutex_unlock(&task->task_mutex);
        
        // Process the task based on type
        switch (task->type) {
            case TASK_UPLOAD:
                handle_upload_task(task);
                break;
            case TASK_DOWNLOAD:
                handle_download_task(task);
                break;
            case TASK_DELETE:
                handle_delete_task(task);
                break;
            case TASK_LIST:
                handle_list_task(task);
                break;
            case TASK_SHUTDOWN:
                printf("Worker thread %lu received shutdown task\n", pthread_self());
                pthread_mutex_lock(&task->task_mutex);
                task->status = TASK_COMPLETED;
                task->result_code = 0;
                pthread_cond_signal(&task->task_cond);
                pthread_mutex_unlock(&task->task_mutex);
                return NULL;
            default:
                printf("Worker thread %lu: Unknown task type %d\n", pthread_self(), task->type);
                pthread_mutex_lock(&task->task_mutex);
                task->status = TASK_ERROR;
                task->result_code = -1;
                strncpy(task->error_message, "Unknown task type", sizeof(task->error_message) - 1);
                pthread_cond_signal(&task->task_cond);
                pthread_mutex_unlock(&task->task_mutex);
                continue;
        }
        
        // Mark task as completed and notify waiting client thread
        pthread_mutex_lock(&task->task_mutex);
        if (task->status == TASK_IN_PROGRESS) {
            task->status = TASK_COMPLETED;
        }
        pthread_cond_signal(&task->task_cond);
        pthread_mutex_unlock(&task->task_mutex);
        
        printf("Worker thread %lu completed task for user %s\n", pthread_self(), task->username);
    }
    
    printf("Worker thread %lu exiting\n", pthread_self());
    return NULL;
}

// Note: Task handler functions are now implemented in file_operations.c
// The declarations are in dropbox_server.h and implementations are in file_operations.c
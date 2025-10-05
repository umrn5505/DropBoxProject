#ifndef DROPBOX_SERVER_H
#define DROPBOX_SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <semaphore.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>

// Configuration constants
#define PORT 8080
#define MAX_CLIENTS 100
#define CLIENT_THREADPOOL_SIZE 10
#define WORKER_THREADPOOL_SIZE 5
#define QUEUE_SIZE 50
#define BUFFER_SIZE 4096
#define MAX_USERNAME 50
#define MAX_PASSWORD 50
#define MAX_FILENAME 256
#define MAX_COMMAND 512

// Task types for the worker thread pool
typedef enum {
    TASK_UPLOAD,
    TASK_DOWNLOAD,
    TASK_DELETE,
    TASK_LIST,
    TASK_SHUTDOWN
} task_type_t;

// Task status for communication between client and worker threads
typedef enum {
    TASK_PENDING,
    TASK_IN_PROGRESS,
    TASK_COMPLETED,
    TASK_ERROR
} task_status_t;

// Forward declarations
typedef struct client_queue client_queue_t;
typedef struct task_queue task_queue_t;
typedef struct task task_t;
typedef struct user_session user_session_t;

// User session structure to track authenticated users
struct user_session {
    char username[MAX_USERNAME];
    int socket_fd;
    int authenticated;
    pthread_t client_thread_id;
};

// Task structure for worker thread pool
struct task {
    task_type_t type;
    int client_socket;
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];
    char command[MAX_COMMAND];
    char *data;
    size_t data_size;
    
    // Synchronization for task completion
    task_status_t status;
    pthread_mutex_t task_mutex;
    pthread_cond_t task_cond;
    
    // Result data
    char *result_data;
    size_t result_size;
    int result_code; // 0 = success, non-zero = error
    char error_message[BUFFER_SIZE];
    
    // Linked list for queue implementation
    struct task *next;
};

// Thread-safe client queue (FIFO)
struct client_queue {
    int *sockets;
    int front;
    int rear;
    int count;
    int capacity;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

// Thread-safe task queue (FIFO)
struct task_queue {
    task_t *head;
    task_t *tail;
    int count;
    int capacity;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};

// Server context structure
typedef struct {
    client_queue_t *client_queue;
    task_queue_t *task_queue;
    pthread_t *client_threads;
    pthread_t *worker_threads;
    int server_socket;
    int shutdown_flag;
    pthread_mutex_t shutdown_mutex;
} server_context_t;

// Function declarations

// Queue operations
client_queue_t* create_client_queue(int capacity);
void destroy_client_queue(client_queue_t *queue);
int enqueue_client(client_queue_t *queue, int socket_fd);
int dequeue_client(client_queue_t *queue);

task_queue_t* create_task_queue(int capacity);
void destroy_task_queue(task_queue_t *queue);
int enqueue_task(task_queue_t *queue, task_t *task);
task_t* dequeue_task(task_queue_t *queue);

// Task operations
task_t* create_task(task_type_t type, int client_socket, const char *username, const char *command);
void destroy_task(task_t *task);

// Thread pool functions
void* client_thread_function(void *arg);
void* worker_thread_function(void *arg);

// Authentication functions
int authenticate_user(int socket_fd, char *username);
int handle_signup(int socket_fd, const char *username, const char *password);
int handle_login(int socket_fd, const char *username, const char *password);

// Command parsing
int parse_command(const char *command_line, char *command, char *filename);

// Task handler functions (to be implemented by colleagues)
void handle_upload_task(task_t *task);
void handle_download_task(task_t *task);
void handle_delete_task(task_t *task);
void handle_list_task(task_t *task);

// Utility functions
void send_response(int socket_fd, const char *response);
int receive_data(int socket_fd, char *buffer, size_t buffer_size);
void cleanup_server(server_context_t *server);
void signal_shutdown(server_context_t *server);

// Global server context (for signal handlers)
extern server_context_t *g_server_context;

#endif // DROPBOX_SERVER_H
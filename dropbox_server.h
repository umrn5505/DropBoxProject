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

// Quota and file management constants
#define DEFAULT_USER_QUOTA_MB 100
#define MAX_FILE_SIZE_MB 50
#define QUOTA_FILE_SUFFIX ".quota"
#define METADATA_FILE_SUFFIX ".meta"

// Priority system constants
#define PRIORITY_HIGH 1
#define PRIORITY_MEDIUM 2
#define PRIORITY_LOW 3
#define MAX_PRIORITY 3

// Encoding constants
#define ENCODING_NONE 0
#define ENCODING_BASE64 1

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
typedef struct file_metadata file_metadata_t;
typedef struct user_quota user_quota_t;

// File metadata structure
struct file_metadata {
    char filename[MAX_FILENAME];
    size_t file_size;
    time_t created_time;
    time_t modified_time;
    int encoding_type;
    char checksum[65]; // SHA-256 checksum (64 chars + null terminator)
};

// User quota structure
struct user_quota {
    char username[MAX_USERNAME];
    size_t quota_limit_bytes;
    size_t used_bytes;
    int file_count;
    pthread_mutex_t quota_mutex;
};

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
    
    // Priority and encoding support
    int priority;           // 1 = high, 2 = medium, 3 = low
    int encoding_type;      // ENCODING_NONE, ENCODING_BASE64
    time_t creation_time;   // For priority queue ordering
    
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
task_t* create_priority_task(task_type_t type, int client_socket, const char *username, const char *command, int priority);
void destroy_task(task_t *task);

// Priority queue operations (enhanced task queue)
int enqueue_priority_task(task_queue_t *queue, task_t *task);

// Thread pool functions
void* client_thread_function(void *arg);
void* worker_thread_function(void *arg);

// Authentication functions
int authenticate_user(int socket_fd, char *username);
int handle_signup(int socket_fd, const char *username, const char *password);
int handle_login(int socket_fd, const char *username, const char *password);

// Command parsing
int parse_command(const char *command_line, char *command, char *filename);
int parse_priority_command(const char *command_line, char *command, char *filename, int *priority);

// Enhanced task handler functions
void handle_upload_task(task_t *task);
void handle_download_task(task_t *task);
void handle_delete_task(task_t *task);
void handle_list_task(task_t *task);

// File operation helpers
int save_file_to_storage(const char *username, const char *filename, const char *data, size_t data_size, int encoding_type);
int load_file_from_storage(const char *username, const char *filename, char **data, size_t *data_size, int encoding_type);
int delete_file_from_storage(const char *username, const char *filename);
int list_user_files(const char *username, char **file_list, size_t *list_size);

// Quota management functions
user_quota_t* load_user_quota(const char *username);
int save_user_quota(const user_quota_t *quota);
int check_quota_available(const char *username, size_t required_bytes);
int update_quota_usage(const char *username, long long size_delta);
void destroy_user_quota(user_quota_t *quota);
int calculate_quota_usage(user_quota_t *quota);

// File metadata functions
int save_file_metadata(const char *username, const file_metadata_t *metadata);
file_metadata_t* load_file_metadata(const char *username, const char *filename);
void destroy_file_metadata(file_metadata_t *metadata);

// Encoding/Decoding functions
char* base64_encode(const char *data, size_t input_length, size_t *output_length);
char* base64_decode(const char *data, size_t input_length, size_t *output_length);

// Conflict resolution functions
int acquire_file_lock(const char *username, const char *filename);
int release_file_lock(const char *username, const char *filename);

// Utility functions
void send_response(int socket_fd, const char *response);
int receive_data(int socket_fd, char *buffer, size_t buffer_size);
void cleanup_server(server_context_t *server);
void signal_shutdown(server_context_t *server);
char* calculate_sha256(const char *data, size_t data_size);

// Global server context (for signal handlers)
extern server_context_t *g_server_context;

#endif // DROPBOX_SERVER_H
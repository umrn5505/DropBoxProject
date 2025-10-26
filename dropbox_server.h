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


#define PRIORITY_HIGH 1
#define PRIORITY_MEDIUM 2
#define PRIORITY_LOW 3
#define MAX_PRIORITY 3


typedef enum {
    TASK_UPLOAD,
    TASK_DOWNLOAD,
    TASK_DELETE,
    TASK_LIST,
    TASK_SHUTDOWN
} task_type_t;


typedef enum {
    TASK_PENDING,
    TASK_IN_PROGRESS,
    TASK_COMPLETED,
    TASK_ERROR
} task_status_t;


typedef struct client_queue client_queue_t;
typedef struct task_queue task_queue_t;
typedef struct task task_t;
typedef struct user_session user_session_t;
typedef struct file_metadata file_metadata_t;


struct file_metadata {
    char filename[MAX_FILENAME];
    size_t file_size;
    time_t created_time;
    time_t modified_time;
    char checksum[65];
};


struct user_session {
    char username[MAX_USERNAME];
    int socket_fd;
    int authenticated;
    pthread_t client_thread_id;
};


struct task {
    task_type_t type;
    int client_socket;
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];
    char command[MAX_COMMAND];
    char *data;
    size_t data_size;
    

    int priority;           
    int encoding_type;     
    time_t creation_time;
    
    
    task_status_t status;
    pthread_mutex_t task_mutex;
    pthread_cond_t task_cond;
    

    char *result_data;
    size_t result_size;
    int result_code; 
    char error_message[BUFFER_SIZE];
    

    struct task *next;
};


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


struct task_queue {
    task_t *head;
    task_t *tail;
    int count;
    int capacity;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
};


typedef struct {
    client_queue_t *client_queue;
    task_queue_t *task_queue;
    pthread_t *client_threads;
    pthread_t *worker_threads;
    int server_socket;
    int shutdown_flag;
    pthread_mutex_t shutdown_mutex;
} server_context_t;


client_queue_t* create_client_queue(int capacity);
void destroy_client_queue(client_queue_t *queue);
int enqueue_client(client_queue_t *queue, int socket_fd);
int dequeue_client(client_queue_t *queue);

task_queue_t* create_task_queue(int capacity);
void destroy_task_queue(task_queue_t *queue);
int enqueue_task(task_queue_t *queue, task_t *task);
task_t* dequeue_task(task_queue_t *queue);


task_t* create_task(task_type_t type, int client_socket, const char *username, const char *command);
task_t* create_priority_task(task_type_t type, int client_socket, const char *username, const char *command, int priority);
void destroy_task(task_t *task);


int enqueue_priority_task(task_queue_t *queue, task_t *task);

void* client_thread_function(void *arg);
void* worker_thread_function(void *arg);

int authenticate_user(int socket_fd, char *username);
int handle_signup(int socket_fd, const char *username, const char *password);
int handle_login(int socket_fd, const char *username, const char *password);

int parse_command(const char *command_line, char *command, char *filename);
int parse_priority_command(const char *command_line, char *command, char *filename, int *priority);


void handle_upload_task(task_t *task);
void handle_download_task(task_t *task);
void handle_delete_task(task_t *task);
void handle_list_task(task_t *task);


int save_file_to_storage(const char *username, const char *filename, const char *data, size_t data_size);
int load_file_from_storage(const char *username, const char *filename, char **data, size_t *data_size);
int delete_file_from_storage(const char *username, const char *filename);
int list_user_files(const char *username, char **file_list, size_t *list_size);

int save_file_metadata(const char *username, const file_metadata_t *metadata);
file_metadata_t* load_file_metadata(const char *username, const char *filename);
void destroy_file_metadata(file_metadata_t *metadata);


int acquire_file_lock(const char *username, const char *filename);
int release_file_lock(const char *username, const char *filename);


void send_response(int socket_fd, const char *response);
ssize_t send_all(int socket_fd, const void *buf, size_t len);
int receive_data(int socket_fd, char *buffer, size_t buffer_size);
void cleanup_server(server_context_t *server);
void signal_shutdown(server_context_t *server);
char* calculate_sha256(const char *data, size_t data_size);

extern server_context_t *g_server_context;

// Server port (fixed to PORT defined above, default 8080)
extern int g_server_port;

void cleanup_user_mutexes();

#endif

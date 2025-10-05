#include "dropbox_server.h"

// Client Queue Implementation
client_queue_t* create_client_queue(int capacity) {
    client_queue_t *queue = malloc(sizeof(client_queue_t));
    if (!queue) {
        perror("Failed to allocate client queue");
        return NULL;
    }
    
    queue->sockets = malloc(capacity * sizeof(int));
    if (!queue->sockets) {
        perror("Failed to allocate client queue sockets array");
        free(queue);
        return NULL;
    }
    
    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;
    queue->capacity = capacity;
    
    // Initialize synchronization primitives
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        perror("Failed to initialize client queue mutex");
        free(queue->sockets);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        perror("Failed to initialize client queue not_empty condition");
        pthread_mutex_destroy(&queue->mutex);
        free(queue->sockets);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        perror("Failed to initialize client queue not_full condition");
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue->sockets);
        free(queue);
        return NULL;
    }
    
    printf("Client queue created with capacity: %d\n", capacity);
    return queue;
}

void destroy_client_queue(client_queue_t *queue) {
    if (!queue) return;
    
    // Close any remaining sockets
    pthread_mutex_lock(&queue->mutex);
    while (queue->count > 0) {
        int socket_fd = queue->sockets[queue->front];
        if (socket_fd >= 0) {
            close(socket_fd);
        }
        queue->front = (queue->front + 1) % queue->capacity;
        queue->count--;
    }
    pthread_mutex_unlock(&queue->mutex);
    
    // Destroy synchronization primitives
    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
    
    free(queue->sockets);
    free(queue);
    printf("Client queue destroyed\n");
}

int enqueue_client(client_queue_t *queue, int socket_fd) {
    if (!queue) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait while queue is full
    while (queue->count >= queue->capacity) {
        printf("Client queue full, waiting...\n");
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    
    // Add socket to queue
    queue->sockets[queue->rear] = socket_fd;
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->count++;
    
    printf("Client socket %d enqueued, queue size: %d\n", socket_fd, queue->count);
    
    // Signal that queue is not empty
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

int dequeue_client(client_queue_t *queue) {
    if (!queue) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait while queue is empty
    while (queue->count == 0) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }
    
    // Remove socket from queue
    int socket_fd = queue->sockets[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->count--;
    
    printf("Client socket %d dequeued, queue size: %d\n", socket_fd, queue->count);
    
    // Signal that queue is not full
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return socket_fd;
}

// Task Queue Implementation
task_queue_t* create_task_queue(int capacity) {
    task_queue_t *queue = malloc(sizeof(task_queue_t));
    if (!queue) {
        perror("Failed to allocate task queue");
        return NULL;
    }
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->capacity = capacity;
    
    // Initialize synchronization primitives
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        perror("Failed to initialize task queue mutex");
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        perror("Failed to initialize task queue not_empty condition");
        pthread_mutex_destroy(&queue->mutex);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        perror("Failed to initialize task queue not_full condition");
        pthread_cond_destroy(&queue->not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue);
        return NULL;
    }
    
    printf("Task queue created with capacity: %d\n", capacity);
    return queue;
}

void destroy_task_queue(task_queue_t *queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Free all remaining tasks
    task_t *current = queue->head;
    while (current) {
        task_t *next = current->next;
        destroy_task(current);
        current = next;
    }
    
    pthread_mutex_unlock(&queue->mutex);
    
    // Destroy synchronization primitives
    pthread_cond_destroy(&queue->not_full);
    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
    
    free(queue);
    printf("Task queue destroyed\n");
}

int enqueue_task(task_queue_t *queue, task_t *task) {
    if (!queue || !task) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait while queue is full
    while (queue->count >= queue->capacity) {
        printf("Task queue full, waiting...\n");
        pthread_cond_wait(&queue->not_full, &queue->mutex);
    }
    
    // Add task to queue (FIFO)
    task->next = NULL;
    if (queue->tail) {
        queue->tail->next = task;
    } else {
        queue->head = task;
    }
    queue->tail = task;
    queue->count++;
    
    printf("Task enqueued (type: %d), queue size: %d\n", task->type, queue->count);
    
    // Signal that queue is not empty
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

task_t* dequeue_task(task_queue_t *queue) {
    if (!queue) return NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait while queue is empty
    while (queue->count == 0) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }
    
    // Remove task from queue (FIFO)
    task_t *task = queue->head;
    queue->head = task->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    task->next = NULL;
    queue->count--;
    
    printf("Task dequeued (type: %d), queue size: %d\n", task->type, queue->count);
    
    // Signal that queue is not full
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return task;
}

// Task Operations
task_t* create_task(task_type_t type, int client_socket, const char *username, const char *command) {
    task_t *task = malloc(sizeof(task_t));
    if (!task) {
        perror("Failed to allocate task");
        return NULL;
    }
    
    // Initialize task fields
    task->type = type;
    task->client_socket = client_socket;
    strncpy(task->username, username ? username : "", MAX_USERNAME - 1);
    task->username[MAX_USERNAME - 1] = '\0';
    strncpy(task->command, command ? command : "", MAX_COMMAND - 1);
    task->command[MAX_COMMAND - 1] = '\0';
    
    task->data = NULL;
    task->data_size = 0;
    task->status = TASK_PENDING;
    task->result_data = NULL;
    task->result_size = 0;
    task->result_code = 0;
    task->error_message[0] = '\0';
    task->next = NULL;
    
    // Initialize synchronization primitives for task completion
    if (pthread_mutex_init(&task->task_mutex, NULL) != 0) {
        perror("Failed to initialize task mutex");
        free(task);
        return NULL;
    }
    
    if (pthread_cond_init(&task->task_cond, NULL) != 0) {
        perror("Failed to initialize task condition variable");
        pthread_mutex_destroy(&task->task_mutex);
        free(task);
        return NULL;
    }
    
    return task;
}

void destroy_task(task_t *task) {
    if (!task) return;
    
    // Free allocated data
    if (task->data) {
        free(task->data);
    }
    if (task->result_data) {
        free(task->result_data);
    }
    
    // Destroy synchronization primitives
    pthread_cond_destroy(&task->task_cond);
    pthread_mutex_destroy(&task->task_mutex);
    
    free(task);
}

// Utility Functions
void send_response(int socket_fd, const char *response) {
    if (socket_fd < 0 || !response) return;
    
    size_t len = strlen(response);
    ssize_t sent = send(socket_fd, response, len, 0);
    if (sent != (ssize_t)len) {
        perror("Failed to send complete response");
    }
}

int receive_data(int socket_fd, char *buffer, size_t buffer_size) {
    if (socket_fd < 0 || !buffer || buffer_size == 0) return -1;
    
    ssize_t received = recv(socket_fd, buffer, buffer_size - 1, 0);
    if (received <= 0) {
        if (received == 0) {
            printf("Client disconnected (socket %d)\n", socket_fd);
        } else {
            perror("Failed to receive data");
        }
        return -1;
    }
    
    buffer[received] = '\0';
    return received;
}

void signal_shutdown(server_context_t *server) {
    if (!server) return;
    
    pthread_mutex_lock(&server->shutdown_mutex);
    server->shutdown_flag = 1;
    pthread_mutex_unlock(&server->shutdown_mutex);
    
    // Wake up all waiting threads
    pthread_cond_broadcast(&server->client_queue->not_empty);
    pthread_cond_broadcast(&server->task_queue->not_empty);
    
    printf("Shutdown signal sent to all threads\n");
}
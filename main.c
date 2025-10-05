#include "dropbox_server.h"

// Global server context for signal handling
server_context_t *g_server_context = NULL;

// Signal handler for graceful shutdown
void signal_handler(int signum) {
    printf("\nReceived signal %d. Initiating graceful shutdown...\n", signum);
    if (g_server_context) {
        signal_shutdown(g_server_context);
    }
}

// Clean up server resources
void cleanup_server(server_context_t *server) {
    if (!server) return;
    
    printf("Cleaning up server resources...\n");
    
    // Signal shutdown to all threads
    signal_shutdown(server);
    
    // Wait for client threads to finish
    if (server->client_threads) {
        printf("Waiting for client threads to finish...\n");
        for (int i = 0; i < CLIENT_THREADPOOL_SIZE; i++) {
            pthread_join(server->client_threads[i], NULL);
        }
        free(server->client_threads);
    }
    
    // Send shutdown tasks to worker threads and wait for them to finish
    if (server->worker_threads && server->task_queue) {
        printf("Sending shutdown signals to worker threads...\n");
        for (int i = 0; i < WORKER_THREADPOOL_SIZE; i++) {
            task_t *shutdown_task = create_task(TASK_SHUTDOWN, -1, "system", "SHUTDOWN");
            if (shutdown_task) {
                enqueue_task(server->task_queue, shutdown_task);
            }
        }
        
        printf("Waiting for worker threads to finish...\n");
        for (int i = 0; i < WORKER_THREADPOOL_SIZE; i++) {
            pthread_join(server->worker_threads[i], NULL);
        }
        free(server->worker_threads);
    }
    
    // Close server socket
    if (server->server_socket >= 0) {
        close(server->server_socket);
    }
    
    // Destroy queues
    if (server->client_queue) {
        destroy_client_queue(server->client_queue);
    }
    if (server->task_queue) {
        destroy_task_queue(server->task_queue);
    }
    
    // Destroy shutdown mutex
    pthread_mutex_destroy(&server->shutdown_mutex);
    
    free(server);
    printf("Server cleanup completed\n");
}

// Initialize server
server_context_t* init_server() {
    server_context_t *server = malloc(sizeof(server_context_t));
    if (!server) {
        perror("Failed to allocate server context");
        return NULL;
    }
    
    // Initialize fields
    server->client_queue = NULL;
    server->task_queue = NULL;
    server->client_threads = NULL;
    server->worker_threads = NULL;
    server->server_socket = -1;
    server->shutdown_flag = 0;
    
    // Initialize shutdown mutex
    if (pthread_mutex_init(&server->shutdown_mutex, NULL) != 0) {
        perror("Failed to initialize shutdown mutex");
        free(server);
        return NULL;
    }
    
    // Create client queue
    server->client_queue = create_client_queue(QUEUE_SIZE);
    if (!server->client_queue) {
        cleanup_server(server);
        return NULL;
    }
    
    // Create task queue
    server->task_queue = create_task_queue(QUEUE_SIZE);
    if (!server->task_queue) {
        cleanup_server(server);
        return NULL;
    }
    
    // Allocate thread arrays
    server->client_threads = malloc(CLIENT_THREADPOOL_SIZE * sizeof(pthread_t));
    if (!server->client_threads) {
        perror("Failed to allocate client threads array");
        cleanup_server(server);
        return NULL;
    }
    
    server->worker_threads = malloc(WORKER_THREADPOOL_SIZE * sizeof(pthread_t));
    if (!server->worker_threads) {
        perror("Failed to allocate worker threads array");
        cleanup_server(server);
        return NULL;
    }
    
    // Create client thread pool
    printf("Creating client thread pool (%d threads)...\n", CLIENT_THREADPOOL_SIZE);
    for (int i = 0; i < CLIENT_THREADPOOL_SIZE; i++) {
        if (pthread_create(&server->client_threads[i], NULL, client_thread_function, server) != 0) {
            perror("Failed to create client thread");
            cleanup_server(server);
            return NULL;
        }
    }
    
    // Create worker thread pool
    printf("Creating worker thread pool (%d threads)...\n", WORKER_THREADPOOL_SIZE);
    for (int i = 0; i < WORKER_THREADPOOL_SIZE; i++) {
        if (pthread_create(&server->worker_threads[i], NULL, worker_thread_function, server) != 0) {
            perror("Failed to create worker thread");
            cleanup_server(server);
            return NULL;
        }
    }
    
    printf("Server initialized successfully\n");
    return server;
}

// Create server socket and bind to port
int create_server_socket() {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Failed to create server socket");
        return -1;
    }
    
    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Failed to set socket options");
        close(server_socket);
        return -1;
    }
    
    // Set up server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    // Bind socket to address
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind server socket");
        close(server_socket);
        return -1;
    }
    
    // Start listening for connections
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("Failed to listen on server socket");
        close(server_socket);
        return -1;
    }
    
    printf("Server listening on port %d\n", PORT);
    return server_socket;
}

// Main accept loop
void run_accept_loop(server_context_t *server) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    printf("Starting main accept loop...\n");
    
    while (1) {
        // Check for shutdown signal
        pthread_mutex_lock(&server->shutdown_mutex);
        int shutdown = server->shutdown_flag;
        pthread_mutex_unlock(&server->shutdown_mutex);
        
        if (shutdown) {
            printf("Accept loop received shutdown signal\n");
            break;
        }
        
        // Accept new client connection
        int client_socket = accept(server->server_socket, 
                                 (struct sockaddr*)&client_addr, 
                                 &client_addr_len);
        
        if (client_socket < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, check shutdown flag
                continue;
            }
            perror("Failed to accept client connection");
            continue;
        }
        
        // Get client IP address for logging
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("Accepted connection from %s:%d (socket %d)\n", 
               client_ip, ntohs(client_addr.sin_port), client_socket);
        
        // Enqueue client socket for processing by client threads
        if (enqueue_client(server->client_queue, client_socket) != 0) {
            printf("Failed to enqueue client socket %d - closing connection\n", client_socket);
            send_response(client_socket, "ERROR: Server busy, please try again later\n");
            close(client_socket);
        }
    }
    
    printf("Accept loop terminated\n");
}

int main() {
    printf("Starting DropBox Server...\n");
    
    // Set up signal handlers for graceful shutdown
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // Termination request
    
    // Initialize server
    server_context_t *server = init_server();
    if (!server) {
        fprintf(stderr, "Failed to initialize server\n");
        return EXIT_FAILURE;
    }
    
    // Set global server context for signal handler
    g_server_context = server;
    
    // Create server socket
    server->server_socket = create_server_socket();
    if (server->server_socket < 0) {
        cleanup_server(server);
        return EXIT_FAILURE;
    }
    
    printf("DropBox Server started successfully!\n");
    printf("Server configuration:\n");
    printf("  Port: %d\n", PORT);
    printf("  Max clients: %d\n", MAX_CLIENTS);
    printf("  Client thread pool size: %d\n", CLIENT_THREADPOOL_SIZE);
    printf("  Worker thread pool size: %d\n", WORKER_THREADPOOL_SIZE);
    printf("  Queue capacity: %d\n", QUEUE_SIZE);
    
    // Run main accept loop
    run_accept_loop(server);
    
    // Cleanup and exit
    cleanup_server(server);
    g_server_context = NULL;
    
    printf("DropBox Server shut down successfully\n");
    return EXIT_SUCCESS;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <assert.h>

#define SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 8080
#define BUFFER_SIZE 8192
#define MAX_RETRIES 5

// Test statistics
typedef struct {
    int total_operations;
    int successful_operations;
    int failed_operations;
    int upload_count;
    int download_count;
    int delete_count;
    int list_count;
    pthread_mutex_t lock;
} test_stats_t;

test_stats_t global_stats = {0, 0, 0, 0, 0, 0, 0, PTHREAD_MUTEX_INITIALIZER};

static int get_server_port() {
    return DEFAULT_SERVER_PORT;
}

typedef struct {
    int client_id;
    int operations;
    int user_id;  // Multiple clients can share same user
    int session_delay_ms;
} client_config_t;

static ssize_t sock_recv_timeout(int sock, char *buf, size_t buflen, int timeout_sec) {
    struct timeval tv;
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    
    ssize_t r = recv(sock, buf, buflen - 1, 0);
    if (r > 0) buf[r] = '\0';
    return r;
}

static int connect_server_retry() {
    for (int retry = 0; retry < MAX_RETRIES; retry++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            usleep(100000); // 100ms
            continue;
        }
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(get_server_port());
        inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);
        
        if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            return s;
        }
        
        close(s);
        usleep(200000); // 200ms before retry
    }
    return -1;
}

static void send_line(int sock, const char *line) {
    send(sock, line, strlen(line), 0);
}

static void update_stats(int success, int op_type) {
    pthread_mutex_lock(&global_stats.lock);
    global_stats.total_operations++;
    if (success) {
        global_stats.successful_operations++;
        switch(op_type) {
            case 0: global_stats.upload_count++; break;
            case 1: global_stats.list_count++; break;
            case 2: global_stats.download_count++; break;
            case 3: global_stats.delete_count++; break;
        }
    } else {
        global_stats.failed_operations++;
    }
    pthread_mutex_unlock(&global_stats.lock);
}

void *client_thread(void *arg) {
    client_config_t *cfg = (client_config_t*)arg;
    char buf[BUFFER_SIZE];
    int sock = -1;
    int authenticated = 0;
    
    printf("[Client %d] Starting (user_id=%d, ops=%d)\\n", 
           cfg->client_id, cfg->user_id, cfg->operations);
    
    // Optional delay to stagger connection
    if (cfg->session_delay_ms > 0) {
        usleep(cfg->session_delay_ms * 1000);
    }
    
    // Connect to server
    sock = connect_server_retry();
    if (sock < 0) {
        fprintf(stderr, "[Client %d] Failed to connect after retries\\n", cfg->client_id);
        return NULL;
    }
    
    // Read welcome message
    if (sock_recv_timeout(sock, buf, sizeof(buf), 5) <= 0) {
        fprintf(stderr, "[Client %d] No welcome message\\n", cfg->client_id);
        close(sock);
        return NULL;
    }
    
    // Authenticate (SIGNUP or LOGIN)
    char username[64];
    snprintf(username, sizeof(username), "testuser_%d", cfg->user_id);
    
    // Try SIGNUP first
    char signup_cmd[128];
    snprintf(signup_cmd, sizeof(signup_cmd), "SIGNUP %s password123\\n", username);
    send_line(sock, signup_cmd);
    
    if (sock_recv_timeout(sock, buf, sizeof(buf), 5) > 0) {
        if (strstr(buf, "SIGNUP_SUCCESS") != NULL) {
            authenticated = 1;
            printf("[Client %d] Signed up as %s\\n", cfg->client_id, username);
        } else if (strstr(buf, "already exists") != NULL) {
            // User exists, try LOGIN
            char login_cmd[128];
            snprintf(login_cmd, sizeof(login_cmd), "LOGIN %s password123\\n", username);
            send_line(sock, login_cmd);
            
            if (sock_recv_timeout(sock, buf, sizeof(buf), 5) > 0) {
                if (strstr(buf, "LOGIN_SUCCESS") != NULL) {
                    authenticated = 1;
                    printf("[Client %d] Logged in as %s\\n", cfg->client_id, username);
                }
            }
        }
    }
    
    if (!authenticated) {
        fprintf(stderr, "[Client %d] Authentication failed\\n", cfg->client_id);
        close(sock);
        return NULL;
    }
    
    // Perform operations
    for (int i = 0; i < cfg->operations; i++) {
        int action = rand() % 4; // 0: upload, 1: list, 2: download, 3: delete
        int success = 0;
        
        if (action == 0) {
            // UPLOAD operation
            char filename[64];
            snprintf(filename, sizeof(filename), "file_u%d_c%d_op%d.txt", 
                     cfg->user_id, cfg->client_id, i);
            
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "UPLOAD %s\\n", filename);
            send_line(sock, cmd);
            
            ssize_t r = sock_recv_timeout(sock, buf, sizeof(buf), 5);
            if (r > 0 && strstr(buf, "SEND_FILE_DATA") != NULL) {
                // Prepare random data
                int data_size = 128 + (rand() % 1024); // 128-1152 bytes
                char *data = malloc(data_size);
                for (int k = 0; k < data_size; k++) {
                    data[k] = 'A' + (rand() % 26);
                }
                
                // Send size
                size_t ssz = (size_t)data_size;
                send(sock, &ssz, sizeof(size_t), 0);
                
                // Send data
                ssize_t sent = 0;
                while (sent < data_size) {
                    ssize_t w = send(sock, data + sent, data_size - sent, 0);
                    if (w <= 0) break;
                    sent += w;
                }
                free(data);
                
                // Read server response
                r = sock_recv_timeout(sock, buf, sizeof(buf), 5);
                if (r > 0 && (strstr(buf, "UPLOAD_SUCCESS") || strstr(buf, "SUCCESS"))) {
                    success = 1;
                }
            }
            update_stats(success, 0);
            
        } else if (action == 1) {
            // LIST operation
            send_line(sock, "LIST\\n");
            ssize_t r = sock_recv_timeout(sock, buf, sizeof(buf), 5);
            if (r > 0) {
                success = 1;
            }
            update_stats(success, 1);
            
        } else if (action == 2) {
            // DOWNLOAD operation
            char filename[64];
            snprintf(filename, sizeof(filename), "file_u%d_c%d_op%d.txt", 
                     cfg->user_id, rand() % 10, rand() % cfg->operations);
            
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\\n", filename);
            send_line(sock, cmd);
            
            // Try to read size
            ssize_t r = recv(sock, buf, sizeof(size_t), 0);
            if (r == (ssize_t)sizeof(size_t)) {
                size_t file_size = *((size_t*)buf);
                
                if (file_size > 0 && file_size < 10 * 1024 * 1024) { // sanity check
                    // Read file data
                    size_t received = 0;
                    while (received < file_size) {
                        ssize_t n = recv(sock, buf, sizeof(buf), 0);
                        if (n <= 0) break;
                        received += n;
                    }
                    success = 1;
                }
            }
            // Read textual response
            sock_recv_timeout(sock, buf, sizeof(buf), 2);
            update_stats(success, 2);
            
        } else {
            // DELETE operation
            char filename[64];
            snprintf(filename, sizeof(filename), "file_u%d_c%d_op%d.txt", 
                     cfg->user_id, rand() % 10, rand() % cfg->operations);
            
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "DELETE %s\\n", filename);
            send_line(sock, cmd);
            
            ssize_t r = sock_recv_timeout(sock, buf, sizeof(buf), 5);
            if (r > 0 && (strstr(buf, "SUCCESS") || strstr(buf, "deleted"))) {
                success = 1;
            }
            update_stats(success, 3);
        }
        
        // Small delay between operations
        usleep(5000 + (rand() % 10000)); // 5-15ms
    }
    
    // Gracefully disconnect
    send_line(sock, "QUIT\\n");
    close(sock);
    
    printf("[Client %d] Completed all operations\\n", cfg->client_id);
    return NULL;
}

void print_test_summary() {
    printf("\\n");
    printf("========================================\\n");
    printf("  TEST STATISTICS\\n");
    printf("========================================\\n");
    printf("Total Operations:      %d\\n", global_stats.total_operations);
    printf("Successful Operations: %d\\n", global_stats.successful_operations);
    printf("Failed Operations:     %d\\n", global_stats.failed_operations);
    printf("----------------------------------------\\n");
    printf("Uploads:               %d\\n", global_stats.upload_count);
    printf("Downloads:             %d\\n", global_stats.download_count);
    printf("Deletes:               %d\\n", global_stats.delete_count);
    printf("Lists:                 %d\\n", global_stats.list_count);
    printf("----------------------------------------\\n");
    
    if (global_stats.total_operations > 0) {
        float success_rate = (float)global_stats.successful_operations / 
                            global_stats.total_operations * 100.0;
        printf("Success Rate:          %.2f%%\\n", success_rate);
    }
    printf("========================================\\n");
}

int main(int argc, char **argv) {
    int num_clients = 30;
    int ops_per_client = 50;
    int num_users = 5; // Multiple clients share users
    
    if (argc >= 2) num_clients = atoi(argv[1]);
    if (argc >= 3) ops_per_client = atoi(argv[2]);
    if (argc >= 4) num_users = atoi(argv[3]);
    
    srand(time(NULL));
    
    printf("========================================\\n");
    printf("  ENHANCED CONCURRENCY TEST\\n");
    printf("========================================\\n");
    printf("Clients:           %d\\n", num_clients);
    printf("Operations/Client: %d\\n", ops_per_client);
    printf("Unique Users:      %d\\n", num_users);
    printf("Total Operations:  %d\\n", num_clients * ops_per_client);
    printf("========================================\\n\\n");
    
    pthread_t *threads = malloc(sizeof(pthread_t) * num_clients);
    client_config_t *configs = malloc(sizeof(client_config_t) * num_clients);
    
    // Create and start client threads
    for (int i = 0; i < num_clients; i++) {
        configs[i].client_id = i + 1;
        configs[i].operations = ops_per_client;
        configs[i].user_id = (i % num_users) + 1; // Multiple clients share users
        configs[i].session_delay_ms = (i % 5) * 100; // Stagger connections
        
        if (pthread_create(&threads[i], NULL, client_thread, &configs[i]) != 0) {
            fprintf(stderr, "Failed to create thread %d\\n", i);
            exit(1);
        }
    }
    
    // Wait for all threads to complete
    for (int i = 0; i < num_clients; i++) {
        pthread_join(threads[i], NULL);
    }
    
    free(threads);
    free(configs);
    
    print_test_summary();
    
    // Return success if >90% operations succeeded
    if (global_stats.total_operations > 0) {
        float success_rate = (float)global_stats.successful_operations / 
                            global_stats.total_operations;
        if (success_rate >= 0.90) {
            printf("\\n✓ TEST PASSED (%.2f%% success rate)\\n", success_rate * 100);
            return 0;
        } else {
            printf("\\n✗ TEST FAILED (%.2f%% success rate, expected >= 90%%)\\n", 
                   success_rate * 100);
            return 1;
        }
    }
    
    return 0;
}

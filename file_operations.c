#include "dropbox_server.h"
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#define MAX_FILE_SIZE_MB 10

static int recv_all(int sock, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t n = recv(sock, (char*)buf + total, len - total, 0);
        if (n <= 0) return -1; // connection closed or error
        total += (size_t)n;
    }
    return 0;
}

void sanitize_filename_inplace(char *name) {
    if (!name) return;
    // Extract basename
    char *last_slash = strrchr(name, '/');
    if (last_slash && *(last_slash + 1) != '\0') {
        memmove(name, last_slash + 1, strlen(last_slash + 1) + 1);
    }
    // Remove any occurrences of ".." (very basic traversal guard)
    while (strstr(name, "..")) {
        char *p = strstr(name, "..");
        memmove(p, p + 2, strlen(p + 2) + 1);
    }
    // If becomes empty, set default
    if (name[0] == '\0') strcpy(name, "unnamed");
}

void handle_upload_task(task_t *task) {
    printf("Processing UPLOAD task for file %s (user: %s, priority: %d)\n",
           task->filename, task->username, task->priority);
    // Sanitize filename early so all subsequent logic (locks, metadata) uses safe form
    sanitize_filename_inplace(task->filename);

    task->result_code = 0;

    pthread_mutex_lock(&task->task_mutex);
    
    
    if (strlen(task->filename) == 0) {
        task->result_code = -1;
        strncpy(task->error_message, "No filename provided for upload", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    if (acquire_file_lock(task->username, task->filename) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "File is currently being accessed by another operation", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
<<<<<<< HEAD
=======

    // TEMPORARY: Artificial delay to demonstrate lock contention and colored output
    sleep(5);
>>>>>>> 41fcdf1d0a6da8a0d825f87123969c4464751410
    
    
    char buffer[BUFFER_SIZE];
    size_t total_received = 0;
    char *file_data = NULL;
    
    
    send_response(task->client_socket, "SEND_FILE_DATA\n");

    
    file_data = malloc(MAX_FILE_SIZE_MB * 1024 * 1024); 
    if (!file_data) {
        task->result_code = -1;
        strncpy(task->error_message, "Memory allocation failed", sizeof(task->error_message) - 1);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    if (recv_all(task->client_socket, buffer, sizeof(size_t)) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "Failed to receive file size", sizeof(task->error_message) - 1);
        free(file_data);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    for (size_t i = 0; i < sizeof(size_t); ++i) printf("%02x ", (unsigned char)buffer[i]);
    printf("\n");
    size_t expected_size = *((size_t*)buffer);
    if (expected_size > MAX_FILE_SIZE_MB * 1024 * 1024) {
        task->result_code = -1;
        strncpy(task->error_message, "File too large", sizeof(task->error_message) - 1);
        free(file_data);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    while (total_received < expected_size) {
        ssize_t bytes_received = recv(task->client_socket, file_data + total_received, expected_size - total_received, 0);
        if (bytes_received <= 0) {
            task->result_code = -1;
            strncpy(task->error_message, "Failed to receive file data", sizeof(task->error_message) - 1);
            free(file_data);
            release_file_lock(task->username, task->filename);
            pthread_mutex_unlock(&task->task_mutex);
            return;
        }
        total_received += bytes_received;
    }
    int save_result = save_file_to_storage(task->username, task->filename, file_data, total_received);
    printf("DEBUG: After save_file_to_storage: result=%d\n", save_result);
    if (save_result != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "Failed to save file", sizeof(task->error_message) - 1);
        free(file_data);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    file_metadata_t metadata;
    strncpy(metadata.filename, task->filename, MAX_FILENAME - 1);
    metadata.filename[MAX_FILENAME - 1] = '\0';
    metadata.file_size = total_received;
    metadata.created_time = time(NULL);
    metadata.modified_time = metadata.created_time;
    
    
    char *checksum = calculate_sha256(file_data, total_received);
    if (checksum) {
        strncpy(metadata.checksum, checksum, sizeof(metadata.checksum) - 1);
        metadata.checksum[sizeof(metadata.checksum) - 1] = '\0';
        free(checksum);
    }
    
    save_file_metadata(task->username, &metadata);
    
    
    task->result_code = 0;
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg), "File '%s' uploaded successfully (%zu bytes)", 
             task->filename, total_received);
    strncpy(task->error_message, success_msg, sizeof(task->error_message) - 1);
    
    free(file_data);
    release_file_lock(task->username, task->filename);
    pthread_mutex_unlock(&task->task_mutex);
}

void handle_download_task(task_t *task) {
    printf("Processing DOWNLOAD task for file %s (user: %s, priority: %d)\n", 
           task->filename, task->username, task->priority);
    
    pthread_mutex_lock(&task->task_mutex);
    
    
    if (strlen(task->filename) == 0) {
        task->result_code = -1;
        strncpy(task->error_message, "No filename provided for download", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    if (acquire_file_lock(task->username, task->filename) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "File is currently being accessed by another operation", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    char *file_data = NULL;
    size_t file_size = 0;
    
    if (load_file_from_storage(task->username, task->filename, &file_data, &file_size) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "File not found or access error", sizeof(task->error_message) - 1);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    if (send(task->client_socket, &file_size, sizeof(size_t), 0) != sizeof(size_t)) {
        task->result_code = -1;
        strncpy(task->error_message, "Failed to send file size", sizeof(task->error_message) - 1);
        free(file_data);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    size_t total_sent = 0;
    while (total_sent < file_size) {
        ssize_t bytes_sent = send(task->client_socket, file_data + total_sent, 
                                file_size - total_sent, 0);
        if (bytes_sent <= 0) {
            task->result_code = -1;
            strncpy(task->error_message, "Failed to send file data", sizeof(task->error_message) - 1);
            free(file_data);
            release_file_lock(task->username, task->filename);
            pthread_mutex_unlock(&task->task_mutex);
            return;
        }
        total_sent += bytes_sent;
    }
    
    
    task->result_code = 0;
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg), "File '%s' downloaded successfully (%zu bytes)", 
             task->filename, file_size);
    strncpy(task->error_message, success_msg, sizeof(task->error_message) - 1);
    
    free(file_data);
    release_file_lock(task->username, task->filename);
    pthread_mutex_unlock(&task->task_mutex);
}

void handle_delete_task(task_t *task) {
    printf("Processing DELETE task for file %s (user: %s, priority: %d)\n", 
           task->filename, task->username, task->priority);
    
    pthread_mutex_lock(&task->task_mutex);
    
    
    if (strlen(task->filename) == 0) {
        task->result_code = -1;
        strncpy(task->error_message, "No filename provided for delete", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    if (acquire_file_lock(task->username, task->filename) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "File is currently being accessed by another operation", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    file_metadata_t *metadata = load_file_metadata(task->username, task->filename);
    size_t file_size = 0;
    if (metadata) {
        file_size = metadata->file_size;
        destroy_file_metadata(metadata);
    }
    
    
    if (delete_file_from_storage(task->username, task->filename) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "File not found or delete failed", sizeof(task->error_message) - 1);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    task->result_code = 0;
    char success_msg[256];
    snprintf(success_msg, sizeof(success_msg), "File '%s' deleted successfully", task->filename);
    strncpy(task->error_message, success_msg, sizeof(task->error_message) - 1);
    
    release_file_lock(task->username, task->filename);
    pthread_mutex_unlock(&task->task_mutex);
}

void handle_list_task(task_t *task) {
    printf("Processing LIST task (user: %s, priority: %d)\n", task->username, task->priority);
    
    pthread_mutex_lock(&task->task_mutex);
    
    
    char *file_list = NULL;
    size_t list_size = 0;
    
    if (list_user_files(task->username, &file_list, &list_size) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "Failed to list files", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    
    task->result_data = file_list;
    task->result_size = list_size;
    task->result_code = 0;
    strncpy(task->error_message, "File list retrieved successfully", sizeof(task->error_message) - 1);
    
    pthread_mutex_unlock(&task->task_mutex);
}
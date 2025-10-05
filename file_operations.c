#include "dropbox_server.h"
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

// Enhanced file operation implementations

void handle_upload_task(task_t *task) {
    printf("Processing UPLOAD task for file %s (user: %s, priority: %d)\n", 
           task->filename, task->username, task->priority);
    
    pthread_mutex_lock(&task->task_mutex);
    
    // Check if filename is provided
    if (strlen(task->filename) == 0) {
        task->result_code = -1;
        strncpy(task->error_message, "No filename provided for upload", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Check quota before upload
    size_t file_size = task->data_size;
    if (!check_quota_available(task->username, file_size)) {
        task->result_code = -1;
        strncpy(task->error_message, "Upload would exceed quota limit", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Acquire file lock for conflict resolution
    if (acquire_file_lock(task->username, task->filename) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "File is currently being accessed by another operation", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // For uploads, we need to receive the file data from the client
    char buffer[BUFFER_SIZE];
    size_t total_received = 0;
    char *file_data = NULL;
    
    // Send request for file data
    send_response(task->client_socket, "SEND_FILE_DATA\n");
    
    // Receive file data in chunks
    file_data = malloc(MAX_FILE_SIZE_MB * 1024 * 1024); // Allocate max file size
    if (!file_data) {
        task->result_code = -1;
        strncpy(task->error_message, "Memory allocation failed", sizeof(task->error_message) - 1);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Receive file size first
    if (recv(task->client_socket, buffer, sizeof(size_t), 0) != sizeof(size_t)) {
        task->result_code = -1;
        strncpy(task->error_message, "Failed to receive file size", sizeof(task->error_message) - 1);
        free(file_data);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    size_t expected_size = *((size_t*)buffer);
    if (expected_size > MAX_FILE_SIZE_MB * 1024 * 1024) {
        task->result_code = -1;
        strncpy(task->error_message, "File too large", sizeof(task->error_message) - 1);
        free(file_data);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Receive file data
    while (total_received < expected_size) {
        ssize_t bytes_received = recv(task->client_socket, file_data + total_received, 
                                    expected_size - total_received, 0);
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
    
    // Save file to storage
    if (save_file_to_storage(task->username, task->filename, file_data, total_received, task->encoding_type) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "Failed to save file", sizeof(task->error_message) - 1);
        free(file_data);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Update quota usage
    update_quota_usage(task->username, (long long)total_received);
    
    // Create and save file metadata
    file_metadata_t metadata;
    strncpy(metadata.filename, task->filename, MAX_FILENAME - 1);
    metadata.filename[MAX_FILENAME - 1] = '\0';
    metadata.file_size = total_received;
    metadata.created_time = time(NULL);
    metadata.modified_time = metadata.created_time;
    metadata.encoding_type = task->encoding_type;
    
    // Calculate checksum
    char *checksum = calculate_sha256(file_data, total_received);
    if (checksum) {
        strncpy(metadata.checksum, checksum, sizeof(metadata.checksum) - 1);
        metadata.checksum[sizeof(metadata.checksum) - 1] = '\0';
        free(checksum);
    }
    
    save_file_metadata(task->username, &metadata);
    
    // Success
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
    
    // Check if filename is provided
    if (strlen(task->filename) == 0) {
        task->result_code = -1;
        strncpy(task->error_message, "No filename provided for download", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Acquire file lock for conflict resolution
    if (acquire_file_lock(task->username, task->filename) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "File is currently being accessed by another operation", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Load file from storage
    char *file_data = NULL;
    size_t file_size = 0;
    
    if (load_file_from_storage(task->username, task->filename, &file_data, &file_size, task->encoding_type) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "File not found or access error", sizeof(task->error_message) - 1);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Send file size first
    if (send(task->client_socket, &file_size, sizeof(size_t), 0) != sizeof(size_t)) {
        task->result_code = -1;
        strncpy(task->error_message, "Failed to send file size", sizeof(task->error_message) - 1);
        free(file_data);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Send file data
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
    
    // Success
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
    
    // Check if filename is provided
    if (strlen(task->filename) == 0) {
        task->result_code = -1;
        strncpy(task->error_message, "No filename provided for delete", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Acquire file lock for conflict resolution
    if (acquire_file_lock(task->username, task->filename) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "File is currently being accessed by another operation", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Load file metadata to get file size for quota update
    file_metadata_t *metadata = load_file_metadata(task->username, task->filename);
    size_t file_size = 0;
    if (metadata) {
        file_size = metadata->file_size;
        destroy_file_metadata(metadata);
    }
    
    // Delete file from storage
    if (delete_file_from_storage(task->username, task->filename) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "File not found or delete failed", sizeof(task->error_message) - 1);
        release_file_lock(task->username, task->filename);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Update quota usage (decrease)
    if (file_size > 0) {
        update_quota_usage(task->username, -((long long)file_size));
    }
    
    // Success
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
    
    // Get user's file list
    char *file_list = NULL;
    size_t list_size = 0;
    
    if (list_user_files(task->username, &file_list, &list_size) != 0) {
        task->result_code = -1;
        strncpy(task->error_message, "Failed to list files", sizeof(task->error_message) - 1);
        pthread_mutex_unlock(&task->task_mutex);
        return;
    }
    
    // Set result data
    task->result_data = file_list;
    task->result_size = list_size;
    task->result_code = 0;
    strncpy(task->error_message, "File list retrieved successfully", sizeof(task->error_message) - 1);
    
    pthread_mutex_unlock(&task->task_mutex);
}
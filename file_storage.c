#include "dropbox_server.h"
#include <dirent.h>
#include <sys/stat.h>

// File storage operations

int save_file_to_storage(const char *username, const char *filename, const char *data, size_t data_size, int encoding_type) {
    if (!username || !filename || !data) return -1;
    
    // Ensure user directory exists
    char user_dir[512];
    snprintf(user_dir, sizeof(user_dir), "storage/%s", username);
    
    struct stat st = {0};
    if (stat("storage", &st) == -1) {
        if (mkdir("storage", 0700) != 0) return -1;
    }
    
    if (stat(user_dir, &st) == -1) {
        if (mkdir(user_dir, 0700) != 0) return -1;
    }
    
    // Create file path
    char file_path[768];
    snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, filename);
    
    // Process data based on encoding type
    char *processed_data = NULL;
    size_t processed_size = data_size;
    
    if (encoding_type == ENCODING_BASE64) {
        // Decode base64 data before storing
        processed_data = base64_decode(data, data_size, &processed_size);
        if (!processed_data) return -1;
    } else {
        processed_data = (char*)data; // Use data directly
    }
    
    // Write file
    FILE *file = fopen(file_path, "wb");
    if (!file) {
        if (encoding_type == ENCODING_BASE64) free(processed_data);
        return -1;
    }
    
    size_t written = fwrite(processed_data, 1, processed_size, file);
    fclose(file);
    
    if (encoding_type == ENCODING_BASE64) free(processed_data);
    
    return (written == processed_size) ? 0 : -1;
}

int load_file_from_storage(const char *username, const char *filename, char **data, size_t *data_size, int encoding_type) {
    if (!username || !filename || !data || !data_size) return -1;
    
    char file_path[768];
    snprintf(file_path, sizeof(file_path), "storage/%s/%s", username, filename);
    
    // Check if file exists
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0) return -1;
    
    // Read file
    FILE *file = fopen(file_path, "rb");
    if (!file) return -1;
    
    size_t file_size = file_stat.st_size;
    char *file_data = malloc(file_size);
    if (!file_data) {
        fclose(file);
        return -1;
    }
    
    size_t read_size = fread(file_data, 1, file_size, file);
    fclose(file);
    
    if (read_size != file_size) {
        free(file_data);
        return -1;
    }
    
    // Process data based on encoding type
    if (encoding_type == ENCODING_BASE64) {
        // Encode data as base64 before sending
        char *encoded_data = base64_encode(file_data, file_size, data_size);
        free(file_data);
        if (!encoded_data) return -1;
        *data = encoded_data;
    } else {
        *data = file_data;
        *data_size = file_size;
    }
    
    return 0;
}

int delete_file_from_storage(const char *username, const char *filename) {
    if (!username || !filename) return -1;
    
    char file_path[768];
    snprintf(file_path, sizeof(file_path), "storage/%s/%s", username, filename);
    
    // Delete the file
    if (unlink(file_path) != 0) return -1;
    
    // Delete associated metadata file
    char meta_path[768];
    snprintf(meta_path, sizeof(meta_path), "storage/%s/%s%s", username, filename, METADATA_FILE_SUFFIX);
    unlink(meta_path); // Don't check result, metadata might not exist
    
    return 0;
}

int list_user_files(const char *username, char **file_list, size_t *list_size) {
    if (!username || !file_list || !list_size) return -1;
    
    char user_dir[512];
    snprintf(user_dir, sizeof(user_dir), "storage/%s", username);
    
    DIR *dir = opendir(user_dir);
    if (!dir) {
        // Create empty list
        *file_list = malloc(256);
        if (!*file_list) return -1;
        strcpy(*file_list, "No files found.\n");
        *list_size = strlen(*file_list);
        return 0;
    }
    
    // Build file list with metadata
    size_t buffer_size = BUFFER_SIZE * 4; // Start with larger buffer
    char *list = malloc(buffer_size);
    if (!list) {
        closedir(dir);
        return -1;
    }
    
    size_t current_pos = 0;
    struct dirent *entry;
    user_quota_t *quota = load_user_quota(username);
    
    // Add header with quota information
    if (quota) {
        current_pos += snprintf(list + current_pos, buffer_size - current_pos,
                              "=== File Listing for %s ===\n"
                              "Quota: %zu / %zu MB (%d files)\n\n"
                              "%-30s %-10s %-20s %-10s\n"
                              "%-30s %-10s %-20s %-10s\n",
                              username,
                              quota->used_bytes / (1024 * 1024),
                              quota->quota_limit_bytes / (1024 * 1024),
                              quota->file_count,
                              "Filename", "Size", "Modified", "Encoding",
                              "--------", "----", "--------", "--------");
        destroy_user_quota(quota);
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; // Skip . and ..
        
        // Skip metadata and quota files
        if (strstr(entry->d_name, METADATA_FILE_SUFFIX) || 
            strstr(entry->d_name, QUOTA_FILE_SUFFIX)) {
            continue;
        }
        
        // Get file stats
        char file_path[768];
        snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, entry->d_name);
        
        struct stat file_stat;
        if (stat(file_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
            continue;
        }
        
        // Load metadata if available
        file_metadata_t *metadata = load_file_metadata(username, entry->d_name);
        
        // Format file information
        char time_str[32];
        time_t mod_time = metadata ? metadata->modified_time : file_stat.st_mtime;
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&mod_time));
        
        const char *encoding_str = "None";
        if (metadata && metadata->encoding_type == ENCODING_BASE64) {
            encoding_str = "Base64";
        }
        
        // Check if we need to expand buffer
        size_t needed = strlen(entry->d_name) + 100; // Rough estimate
        if (current_pos + needed >= buffer_size) {
            buffer_size *= 2;
            char *new_list = realloc(list, buffer_size);
            if (!new_list) {
                free(list);
                closedir(dir);
                if (metadata) destroy_file_metadata(metadata);
                return -1;
            }
            list = new_list;
        }
        
        current_pos += snprintf(list + current_pos, buffer_size - current_pos,
                              "%-30s %-10ld %-20s %-10s\n",
                              entry->d_name,
                              file_stat.st_size,
                              time_str,
                              encoding_str);
        
        if (metadata) destroy_file_metadata(metadata);
    }
    
    closedir(dir);
    
    if (current_pos == 0) {
        strcpy(list, "No files found.\n");
        current_pos = strlen(list);
    }
    
    *file_list = list;
    *list_size = current_pos;
    
    return 0;
}

// File metadata functions
int save_file_metadata(const char *username, const file_metadata_t *metadata) {
    if (!username || !metadata) return -1;
    
    char meta_path[768];
    snprintf(meta_path, sizeof(meta_path), "storage/%s/%s%s", 
             username, metadata->filename, METADATA_FILE_SUFFIX);
    
    FILE *file = fopen(meta_path, "w");
    if (!file) return -1;
    
    fprintf(file, "%s\n%zu\n%ld\n%ld\n%d\n%s\n",
            metadata->filename,
            metadata->file_size,
            metadata->created_time,
            metadata->modified_time,
            metadata->encoding_type,
            metadata->checksum);
    
    fclose(file);
    return 0;
}

file_metadata_t* load_file_metadata(const char *username, const char *filename) {
    if (!username || !filename) return NULL;
    
    char meta_path[768];
    snprintf(meta_path, sizeof(meta_path), "storage/%s/%s%s", 
             username, filename, METADATA_FILE_SUFFIX);
    
    FILE *file = fopen(meta_path, "r");
    if (!file) return NULL;
    
    file_metadata_t *metadata = malloc(sizeof(file_metadata_t));
    if (!metadata) {
        fclose(file);
        return NULL;
    }
    
    if (fscanf(file, "%255s\n%zu\n%ld\n%ld\n%d\n%64s\n",
               metadata->filename,
               &metadata->file_size,
               &metadata->created_time,
               &metadata->modified_time,
               &metadata->encoding_type,
               metadata->checksum) != 6) {
        free(metadata);
        fclose(file);
        return NULL;
    }
    
    fclose(file);
    return metadata;
}

void destroy_file_metadata(file_metadata_t *metadata) {
    if (metadata) {
        free(metadata);
    }
}
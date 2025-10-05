#include "dropbox_server.h"
#include <dirent.h>
#include <sys/stat.h>

// Quota management implementation

user_quota_t* load_user_quota(const char *username) {
    if (!username) return NULL;
    
    user_quota_t *quota = malloc(sizeof(user_quota_t));
    if (!quota) return NULL;
    
    // Initialize quota structure
    strncpy(quota->username, username, MAX_USERNAME - 1);
    quota->username[MAX_USERNAME - 1] = '\0';
    quota->quota_limit_bytes = DEFAULT_USER_QUOTA_MB * 1024 * 1024; // Default 100MB
    quota->used_bytes = 0;
    quota->file_count = 0;
    
    if (pthread_mutex_init(&quota->quota_mutex, NULL) != 0) {
        free(quota);
        return NULL;
    }
    
    // Try to load existing quota file
    char quota_file[512];
    snprintf(quota_file, sizeof(quota_file), "storage/%s%s", username, QUOTA_FILE_SUFFIX);
    
    FILE *file = fopen(quota_file, "r");
    if (file) {
        fscanf(file, "%zu %zu %d", &quota->quota_limit_bytes, &quota->used_bytes, &quota->file_count);
        fclose(file);
    } else {
        // Calculate current usage by scanning user directory
        calculate_quota_usage(quota);
    }
    
    return quota;
}

int save_user_quota(const user_quota_t *quota) {
    if (!quota) return -1;
    
    // Ensure storage directory exists
    struct stat st = {0};
    if (stat("storage", &st) == -1) {
        if (mkdir("storage", 0700) != 0) {
            return -1;
        }
    }
    
    char quota_file[512];
    snprintf(quota_file, sizeof(quota_file), "storage/%s%s", quota->username, QUOTA_FILE_SUFFIX);
    
    FILE *file = fopen(quota_file, "w");
    if (!file) return -1;
    
    fprintf(file, "%zu %zu %d", quota->quota_limit_bytes, quota->used_bytes, quota->file_count);
    fclose(file);
    
    return 0;
}

int check_quota_available(const char *username, size_t required_bytes) {
    user_quota_t *quota = load_user_quota(username);
    if (!quota) return 0;
    
    pthread_mutex_lock(&quota->quota_mutex);
    int available = (quota->used_bytes + required_bytes) <= quota->quota_limit_bytes;
    pthread_mutex_unlock(&quota->quota_mutex);
    
    destroy_user_quota(quota);
    return available;
}

int update_quota_usage(const char *username, long long size_delta) {
    user_quota_t *quota = load_user_quota(username);
    if (!quota) return -1;
    
    pthread_mutex_lock(&quota->quota_mutex);
    
    if (size_delta > 0) {
        quota->used_bytes += (size_t)size_delta;
        quota->file_count++;
    } else if (size_delta < 0) {
        size_t decrease = (size_t)(-size_delta);
        quota->used_bytes = (quota->used_bytes > decrease) ? (quota->used_bytes - decrease) : 0;
        if (quota->file_count > 0) quota->file_count--;
    }
    
    pthread_mutex_unlock(&quota->quota_mutex);
    
    int result = save_user_quota(quota);
    destroy_user_quota(quota);
    
    return result;
}

void destroy_user_quota(user_quota_t *quota) {
    if (!quota) return;
    
    pthread_mutex_destroy(&quota->quota_mutex);
    free(quota);
}

// Helper function to calculate quota usage by scanning directory
int calculate_quota_usage(user_quota_t *quota) {
    if (!quota) return -1;
    
    char user_dir[512];
    snprintf(user_dir, sizeof(user_dir), "storage/%s", quota->username);
    
    DIR *dir = opendir(user_dir);
    if (!dir) {
        // Directory doesn't exist, create it
        if (mkdir(user_dir, 0700) != 0) {
            return -1;
        }
        quota->used_bytes = 0;
        quota->file_count = 0;
        return 0;
    }
    
    struct dirent *entry;
    struct stat file_stat;
    quota->used_bytes = 0;
    quota->file_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; // Skip . and ..
        
        // Skip metadata and quota files
        if (strstr(entry->d_name, METADATA_FILE_SUFFIX) || 
            strstr(entry->d_name, QUOTA_FILE_SUFFIX)) {
            continue;
        }
        
        char file_path[768];
        snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, entry->d_name);
        
        if (stat(file_path, &file_stat) == 0 && S_ISREG(file_stat.st_mode)) {
            quota->used_bytes += file_stat.st_size;
            quota->file_count++;
        }
    }
    
    closedir(dir);
    return 0;
}
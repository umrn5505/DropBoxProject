#include "dropbox_server.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>

#define METADATA_FILE_SUFFIX ".meta"
#define USER_QUOTA_META_SUFFIX ".quota.meta"
#define USER_QUOTA_MB 50 // 50 MB quota per user

static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const int mod_table[] = {0, 2, 1};

char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
    *output_length = 4 * ((input_length + 2) / 3);
    char *encoded_data = malloc(*output_length + 1);
    if (!encoded_data) return NULL;
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        encoded_data[j++] = base64_table[(triple >> 18) & 0x3F];
        encoded_data[j++] = base64_table[(triple >> 12) & 0x3F];
        encoded_data[j++] = base64_table[(triple >> 6) & 0x3F];
        encoded_data[j++] = base64_table[triple & 0x3F];
    }
    for (size_t i = 0; i < (size_t)mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';
    encoded_data[*output_length] = '\0';
    return encoded_data;
}

unsigned char *base64_decode(const char *data, size_t input_length, size_t *output_length) {
    if (input_length % 4 != 0) return NULL;
    *output_length = input_length / 4 * 3;
    if (data[input_length - 1] == '=') (*output_length)--;
    if (data[input_length - 2] == '=') (*output_length)--;
    unsigned char *decoded_data = malloc(*output_length);
    if (!decoded_data) return NULL;
    uint32_t sextet_a, sextet_b, sextet_c, sextet_d;
    for (size_t i = 0, j = 0; i < input_length;) {
        sextet_a = data[i] == '=' ? 0 & i++ : strchr(base64_table, data[i++]) - base64_table;
        sextet_b = data[i] == '=' ? 0 & i++ : strchr(base64_table, data[i++]) - base64_table;
        sextet_c = data[i] == '=' ? 0 & i++ : strchr(base64_table, data[i++]) - base64_table;
        sextet_d = data[i] == '=' ? 0 & i++ : strchr(base64_table, data[i++]) - base64_table;
        uint32_t triple = (sextet_a << 18) | (sextet_b << 12) | (sextet_c << 6) | sextet_d;
        if (j < *output_length) decoded_data[j++] = (triple >> 16) & 0xFF;
        if (j < *output_length) decoded_data[j++] = (triple >> 8) & 0xFF;
        if (j < *output_length) decoded_data[j++] = triple & 0xFF;
    }
    return decoded_data;
}

typedef struct {
    size_t quota_limit;
    size_t used_bytes;
} user_quota_t;

// Load user quota metadata
int load_user_quota(const char *username, user_quota_t *quota) {
    char quota_path[512];
    snprintf(quota_path, sizeof(quota_path), "storage/%s%s", username, USER_QUOTA_META_SUFFIX);
    FILE *file = fopen(quota_path, "r");
    if (!file) {
        quota->quota_limit = USER_QUOTA_MB * 1024 * 1024;
        quota->used_bytes = 0;
        return 0;
    }
    fscanf(file, "%zu\n%zu\n", &quota->quota_limit, &quota->used_bytes);
    fclose(file);
    return 0;
}

// Save user quota metadata
int save_user_quota(const char *username, const user_quota_t *quota) {
    char quota_path[512];
    snprintf(quota_path, sizeof(quota_path), "storage/%s%s", username, USER_QUOTA_META_SUFFIX);
    FILE *file = fopen(quota_path, "w");
    if (!file) return -1;
    fprintf(file, "%zu\n%zu\n", quota->quota_limit, quota->used_bytes);
    fclose(file);
    return 0;
}

// Update quota on file upload
int update_quota_on_upload(const char *username, size_t file_size) {
    user_quota_t quota;
    load_user_quota(username, &quota);
    quota.used_bytes += file_size;
    return save_user_quota(username, &quota);
}

// Update quota on file delete
int update_quota_on_delete(const char *username, size_t file_size) {
    user_quota_t quota;
    load_user_quota(username, &quota);
    if (quota.used_bytes >= file_size) quota.used_bytes -= file_size;
    else quota.used_bytes = 0;
    return save_user_quota(username, &quota);
}

int save_file_to_storage(const char *username, const char *filename, const char *data, size_t data_size) {
    if (!username || !filename || !data) return -1;
    // Quota check
    user_quota_t quota;
    load_user_quota(username, &quota);
    size_t encoded_size = 4 * ((data_size + 2) / 3); // Calculate encoded size without allocating
    if (quota.used_bytes + encoded_size > quota.quota_limit) {
        return -2; // Quota exceeded
    }


    char user_dir[512];
    snprintf(user_dir, sizeof(user_dir), "storage/%s", username);
    
    struct stat st = {0};
    if (stat("storage", &st) == -1) {
        if (mkdir("storage", 0700) != 0) return -1;
    }
    
    if (stat(user_dir, &st) == -1) {
        if (mkdir(user_dir, 0700) != 0) return -1;
    }
    
    
    char file_path[768];
    snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, filename);
    
    
    char *encoded_data = base64_encode((const unsigned char *)data, data_size, &encoded_size);
    if (!encoded_data) return -1;
    FILE *file = fopen(file_path, "wb");
    if (!file) { free(encoded_data); return -1; }
    size_t written = fwrite(encoded_data, 1, encoded_size, file);
    fclose(file);
    free(encoded_data);

    int result = (written == encoded_size) ? 0 : -1;
    if (result == 0) update_quota_on_upload(username, encoded_size);
    return result;
}

int load_file_from_storage(const char *username, const char *filename, char **data, size_t *data_size) {
    if (!username || !filename || !data || !data_size) return -1;
    
    char file_path[768];
    snprintf(file_path, sizeof(file_path), "storage/%s/%s", username, filename);
    
    
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0) return -1;
    
    
    FILE *file = fopen(file_path, "rb");
    if (!file) return -1;
    
    size_t file_size = file_stat.st_size;
    char *encoded_data = malloc(file_size + 1);
    if (!encoded_data) { fclose(file); return -1; }
    size_t read_size = fread(encoded_data, 1, file_size, file);
    fclose(file);
    if (read_size != file_size) { free(encoded_data); return -1; }
    encoded_data[file_size] = '\0';
    size_t decoded_size;
    unsigned char *decoded_data = base64_decode(encoded_data, file_size, &decoded_size);
    free(encoded_data);
    if (!decoded_data) return -1;
    *data = (char *)decoded_data;
    *data_size = decoded_size;

    return 0;
}

int delete_file_from_storage(const char *username, const char *filename) {
    if (!username || !filename) return -1;
    
    char file_path[768];
    snprintf(file_path, sizeof(file_path), "storage/%s/%s", username, filename);
    
    struct stat st;
    size_t file_size = 0;
    if (stat(file_path, &st) == 0 && S_ISREG(st.st_mode)) file_size = st.st_size;
    if (unlink(file_path) != 0) return -1;
    
    char meta_path[768];
    snprintf(meta_path, sizeof(meta_path), "storage/%s/%s%s", username, filename, METADATA_FILE_SUFFIX);
    unlink(meta_path);
    update_quota_on_delete(username, file_size);
    return 0;
}

int list_user_files(const char *username, char **file_list, size_t *list_size) {
    if (!username || !file_list || !list_size) return -1;
    
    char user_dir[512];
    snprintf(user_dir, sizeof(user_dir), "storage/%s", username);
    
    DIR *dir = opendir(user_dir);
    if (!dir) {
       
        *file_list = malloc(256);
        if (!*file_list) return -1;
        strcpy(*file_list, "No files found.\n");
        *list_size = strlen(*file_list);
        return 0;
    }
    
    
    size_t buffer_size = BUFFER_SIZE * 4; 
    char *list = malloc(buffer_size);
    if (!list) {
        closedir(dir);
        return -1;
    }
    
    size_t current_pos = 0;
    struct dirent *entry;
    
    // Simple header without quota information
    current_pos += snprintf(list + current_pos, buffer_size - current_pos,
                          "=== File Listing for %s ===\n\n"
                          "%-30s %-10s %-20s\n"
                          "%-30s %-10s %-20s\n",
                          username,
                          "Filename", "Size", "Modified",
                          "--------", "----", "--------");    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; 
        
        
        if (strstr(entry->d_name, METADATA_FILE_SUFFIX)) {
            continue;
        }
        
        
        char file_path[768];
        snprintf(file_path, sizeof(file_path), "%s/%s", user_dir, entry->d_name);
        
        struct stat file_stat;
        if (stat(file_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
            continue;
        }
        
      
        file_metadata_t *metadata = load_file_metadata(username, entry->d_name);
        
        
        char time_str[32];
        time_t mod_time = metadata ? metadata->modified_time : file_stat.st_mtime;
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&mod_time));
        
       
        size_t needed = strlen(entry->d_name) + 100; 
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
                              "%-30s %-10ld %-20s\n",
                              entry->d_name,
                              file_stat.st_size,
                              time_str);
        
        if (metadata) {
            destroy_file_metadata(metadata);
            metadata = NULL;
        }
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


int save_file_metadata(const char *username, const file_metadata_t *metadata) {
    if (!username || !metadata) return -1;
    
    char meta_path[768];
    snprintf(meta_path, sizeof(meta_path), "storage/%s/%s%s", 
             username, metadata->filename, METADATA_FILE_SUFFIX);
    
    FILE *file = fopen(meta_path, "w");
    if (!file) return -1;
    
    fprintf(file, "%s\n%zu\n%ld\n%ld\n%s\n",
            metadata->filename,
            metadata->file_size,
            metadata->created_time,
            metadata->modified_time,
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
    
    if (fscanf(file, "%255s\n%zu\n%ld\n%ld\n%64s\n",
               metadata->filename,
               &metadata->file_size,
               &metadata->created_time,
               &metadata->modified_time,
               metadata->checksum) != 5) {
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
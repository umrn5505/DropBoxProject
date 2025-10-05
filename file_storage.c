#include "dropbox_server.h"
#include <dirent.h>
#include <sys/stat.h>



int save_file_to_storage(const char *username, const char *filename, const char *data, size_t data_size) {
    if (!username || !filename || !data) return -1;
    
    
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
    
    
    FILE *file = fopen(file_path, "wb");
    if (!file) return -1;
    
    size_t written = fwrite(data, 1, data_size, file);
    fclose(file);
    
    return (written == data_size) ? 0 : -1;
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
    
    
    *data = file_data;
    *data_size = file_size;
    
    return 0;
}

int delete_file_from_storage(const char *username, const char *filename) {
    if (!username || !filename) return -1;
    
    char file_path[768];
    snprintf(file_path, sizeof(file_path), "storage/%s/%s", username, filename);
    
    
    if (unlink(file_path) != 0) return -1;
    
    char meta_path[768];
    snprintf(meta_path, sizeof(meta_path), "storage/%s/%s%s", username, filename, METADATA_FILE_SUFFIX);
    unlink(meta_path); 
    
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
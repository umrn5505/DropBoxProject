#include "dropbox_server.h"
#include <openssl/sha.h>


char* calculate_sha256(const char *data, size_t data_size) {
    if (!data || data_size == 0) return NULL;
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, data_size);
    SHA256_Final(hash, &sha256);
    
    char *hex_string = malloc(SHA256_DIGEST_LENGTH * 2 + 1);
    if (!hex_string) return NULL;
    
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(hex_string + (i * 2), "%02x", hash[i]);
    }
    hex_string[SHA256_DIGEST_LENGTH * 2] = '\0';
    
    return hex_string;
}

static pthread_mutex_t file_locks_mutex = PTHREAD_MUTEX_INITIALIZER;
static char locked_files[MAX_CLIENTS][512]; 
static int locked_files_count = 0;

int acquire_file_lock(const char *username, const char *filename) {
    if (!username || !filename) return -1;
    
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/%s", username, filename);
    
    pthread_mutex_lock(&file_locks_mutex);
<<<<<<< HEAD
    
    
    for (int i = 0; i < locked_files_count; i++) {
        if (strcmp(locked_files[i], file_path) == 0) {
=======
    printf("\033[1;31m[LOCK] ATTEMPTING TO ACQUIRE LOCK FOR %s/%s\033[0m\n", username, filename);

    for (int i = 0; i < locked_files_count; i++) {
        if (strcmp(locked_files[i], file_path) == 0) {
            printf("\033[1;31m[LOCK] WAITING: LOCK ALREADY HELD FOR %s/%s\033[0m\n", username, filename);
>>>>>>> 41fcdf1d0a6da8a0d825f87123969c4464751410
            pthread_mutex_unlock(&file_locks_mutex);
            return -1;
        }
    }
<<<<<<< HEAD
    
   
=======

>>>>>>> 41fcdf1d0a6da8a0d825f87123969c4464751410
    if (locked_files_count < MAX_CLIENTS) {
        strncpy(locked_files[locked_files_count], file_path, sizeof(locked_files[0]) - 1);
        locked_files[locked_files_count][sizeof(locked_files[0]) - 1] = '\0';
        locked_files_count++;
<<<<<<< HEAD
        pthread_mutex_unlock(&file_locks_mutex);
        return 0; 
    }
    
    pthread_mutex_unlock(&file_locks_mutex);
    return -1; 
=======
    printf("\033[1;31m[LOCK HELD] LOCK IS NOW HELD BY %s FOR %s\033[0m\n", username, filename);
        pthread_mutex_unlock(&file_locks_mutex);
        return 0;
    }

    printf("\033[1;31m[LOCK] LOCK TABLE FULL OR ERROR FOR %s/%s\033[0m\n", username, filename);
    pthread_mutex_unlock(&file_locks_mutex);
    return -1;
>>>>>>> 41fcdf1d0a6da8a0d825f87123969c4464751410
}

int release_file_lock(const char *username, const char *filename) {
    if (!username || !filename) return -1;
    
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/%s", username, filename);
    
    pthread_mutex_lock(&file_locks_mutex);
<<<<<<< HEAD
    
 
    for (int i = 0; i < locked_files_count; i++) {
        if (strcmp(locked_files[i], file_path) == 0) {
           
=======
    printf("\033[1;31m[LOCK] ATTEMPTING TO RELEASE LOCK FOR %s/%s\033[0m\n", username, filename);

    for (int i = 0; i < locked_files_count; i++) {
        if (strcmp(locked_files[i], file_path) == 0) {
>>>>>>> 41fcdf1d0a6da8a0d825f87123969c4464751410
            for (int j = i; j < locked_files_count - 1; j++) {
                strcpy(locked_files[j], locked_files[j + 1]);
            }
            locked_files_count--;
<<<<<<< HEAD
=======
            printf("\033[1;31m[LOCK] RELEASED FOR %s/%s\033[0m\n", username, filename);
>>>>>>> 41fcdf1d0a6da8a0d825f87123969c4464751410
            pthread_mutex_unlock(&file_locks_mutex);
            return 0;
        }
    }
<<<<<<< HEAD
    
    pthread_mutex_unlock(&file_locks_mutex);
    return -1; 
=======

    printf("\033[1;31m[LOCK] NOT FOUND FOR %s/%s\033[0m\n", username, filename);
    pthread_mutex_unlock(&file_locks_mutex);
    return -1;
>>>>>>> 41fcdf1d0a6da8a0d825f87123969c4464751410
}
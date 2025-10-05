#include "dropbox_server.h"
#include <openssl/sha.h>
#include <openssl/evp.h>

// Base64 encoding table
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64 decoding table
static const int base64_decode_table[128] = {
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
    52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-2,-1,-1,
    -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
    -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1
};

char* base64_encode(const char *data, size_t input_length, size_t *output_length) {
    if (!data || input_length == 0) return NULL;
    
    *output_length = 4 * ((input_length + 2) / 3);
    char *encoded_data = malloc(*output_length + 1);
    if (!encoded_data) return NULL;
    
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;
        
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        
        encoded_data[j++] = base64_chars[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 0 * 6) & 0x3F];
    }
    
    // Add padding
    for (int i = 0; i < (3 - input_length % 3) % 3; i++) {
        encoded_data[*output_length - 1 - i] = '=';
    }
    
    encoded_data[*output_length] = '\0';
    return encoded_data;
}

char* base64_decode(const char *data, size_t input_length, size_t *output_length) {
    if (!data || input_length == 0 || input_length % 4 != 0) return NULL;
    
    *output_length = input_length / 4 * 3;
    if (data[input_length - 1] == '=') (*output_length)--;
    if (data[input_length - 2] == '=') (*output_length)--;
    
    char *decoded_data = malloc(*output_length + 1);
    if (!decoded_data) return NULL;
    
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t sextet_a = data[i] == '=' ? 0 & i++ : base64_decode_table[(int)data[i++]];
        uint32_t sextet_b = data[i] == '=' ? 0 & i++ : base64_decode_table[(int)data[i++]];
        uint32_t sextet_c = data[i] == '=' ? 0 & i++ : base64_decode_table[(int)data[i++]];
        uint32_t sextet_d = data[i] == '=' ? 0 & i++ : base64_decode_table[(int)data[i++]];
        
        uint32_t triple = (sextet_a << 3 * 6) + (sextet_b << 2 * 6) + (sextet_c << 1 * 6) + (sextet_d << 0 * 6);
        
        if (j < *output_length) decoded_data[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < *output_length) decoded_data[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < *output_length) decoded_data[j++] = (triple >> 0 * 8) & 0xFF;
    }
    
    decoded_data[*output_length] = '\0';
    return decoded_data;
}

// SHA-256 checksum calculation
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

// Conflict resolution - file locking
static pthread_mutex_t file_locks_mutex = PTHREAD_MUTEX_INITIALIZER;
static char locked_files[MAX_CLIENTS][512]; // Store locked file paths
static int locked_files_count = 0;

int acquire_file_lock(const char *username, const char *filename) {
    if (!username || !filename) return -1;
    
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/%s", username, filename);
    
    pthread_mutex_lock(&file_locks_mutex);
    
    // Check if file is already locked
    for (int i = 0; i < locked_files_count; i++) {
        if (strcmp(locked_files[i], file_path) == 0) {
            pthread_mutex_unlock(&file_locks_mutex);
            return -1; // File is locked
        }
    }
    
    // Add to locked files list
    if (locked_files_count < MAX_CLIENTS) {
        strncpy(locked_files[locked_files_count], file_path, sizeof(locked_files[0]) - 1);
        locked_files[locked_files_count][sizeof(locked_files[0]) - 1] = '\0';
        locked_files_count++;
        pthread_mutex_unlock(&file_locks_mutex);
        return 0; // Successfully locked
    }
    
    pthread_mutex_unlock(&file_locks_mutex);
    return -1; // Too many locks
}

int release_file_lock(const char *username, const char *filename) {
    if (!username || !filename) return -1;
    
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/%s", username, filename);
    
    pthread_mutex_lock(&file_locks_mutex);
    
    // Find and remove from locked files list
    for (int i = 0; i < locked_files_count; i++) {
        if (strcmp(locked_files[i], file_path) == 0) {
            // Shift remaining entries
            for (int j = i; j < locked_files_count - 1; j++) {
                strcpy(locked_files[j], locked_files[j + 1]);
            }
            locked_files_count--;
            pthread_mutex_unlock(&file_locks_mutex);
            return 0; // Successfully unlocked
        }
    }
    
    pthread_mutex_unlock(&file_locks_mutex);
    return -1; // File was not locked
}
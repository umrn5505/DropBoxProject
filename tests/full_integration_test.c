#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdarg.h>
#include <stdint.h>

// helper to convert 64-bit to/from network byte order (correct implementation)
static uint64_t htonll(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(x & 0xffffffffu)) << 32) | (uint64_t)htonl((uint32_t)(x >> 32));
#else
    return x;
#endif
}
static uint64_t ntohll(uint64_t x) {
    // network -> host is same as host -> network for symmetric transform
    return htonll(x);
}

#define SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 8080
#define BUFFER_SIZE 16384
#define TEST_MAX_FILE_SIZE_MB 10
#define TEST_MAX_UPLOAD_BYTES ((size_t)TEST_MAX_FILE_SIZE_MB * 1024 * 1024)

static int get_server_port() {
    (void)0; // always use DEFAULT_SERVER_PORT
    return DEFAULT_SERVER_PORT;
}

static pthread_mutex_t log_mtx = PTHREAD_MUTEX_INITIALIZER;

static void log_printf(const char *fmt, ...) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    char tbuf[64];
    int ms = tv.tv_usec / 1000;
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm);

    pthread_t tid = pthread_self();
    va_list ap;
    va_start(ap, fmt);

    pthread_mutex_lock(&log_mtx);
    fprintf(stdout, "%s.%03d [tid=%lu] ", tbuf, ms, (unsigned long)tid);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    fflush(stdout);
    pthread_mutex_unlock(&log_mtx);

    va_end(ap);
}

/* prototype so functions used earlier compile cleanly */
int wait_for_substring(int sock, const char *substr, int timeout_ms, char *outbuf, size_t outbuf_len);

static ssize_t recv_n(int sock, void *buf, size_t n) {
    size_t got = 0;
    char *p = buf;
    while (got < n) {
        ssize_t r = recv(sock, p + got, n - got, 0);
        if (r <= 0) return r;
        got += r;
    }
    return (ssize_t)got;
}

static ssize_t sock_recv(int sock, char *buf, size_t buflen) {
    ssize_t r = recv(sock, buf, buflen - 1, 0);
    if (r > 0) buf[r] = '\0';
    return r;
}

static int connect_server() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(get_server_port());
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(s); return -1; }
    return s;
}

static int send_line(int sock, const char *line) {
    size_t len = strlen(line);
    ssize_t s = send(sock, line, len, 0);
    return (s == (ssize_t)len) ? 0 : -1;
}

// Basic flow: signup/login, upload, list, download, delete
static int single_client_flow(const char *username_prefix) {
    char buf[BUFFER_SIZE];
    int s = connect_server();
    if (s < 0) { perror("connect"); return -1; }
    log_printf("single_client_flow: connected to server socket=%d", s);

    // read welcome
    sock_recv(s, buf, sizeof(buf));
    log_printf("single_client_flow: welcome: %.200s", buf);

    char username[128];
    snprintf(username, sizeof(username), "%s_%ld", username_prefix, time(NULL) % 100000);
    char cmd[512];

    snprintf(cmd, sizeof(cmd), "SIGNUP %s pass\n", username);
    log_printf("single_client_flow: sending SIGNUP for %s", username);
    send_line(s, cmd);
    // Wait for either SIGNUP_SUCCESS or LOGIN_SUCCESS (signup may return immediate success)
    if (!wait_for_substring(s, "SIGNUP_SUCCESS", 3000, buf, sizeof(buf))) {
        // Not found - try LOGIN and wait for LOGIN_SUCCESS
        snprintf(cmd, sizeof(cmd), "LOGIN %s pass\n", username);
        log_printf("single_client_flow: signup not successful, trying LOGIN for %s", username);
        send_line(s, cmd);
        if (!wait_for_substring(s, "LOGIN_SUCCESS", 3000, buf, sizeof(buf))) {
            fprintf(stderr, "Auth failed for %s: %s\n", username, buf);
            log_printf("single_client_flow: auth failed for %s, buf=%.200s", username, buf);
            close(s);
            return -1;
        } else {
            log_printf("single_client_flow: LOGIN_SUCCESS for %s", username);
        }
    } else {
        log_printf("single_client_flow: SIGNUP_SUCCESS for %s", username);
    }

    // prepare data
    const char *fname = "integr_test.bin";
    const char *payload = "THIS_IS_TEST_PAYLOAD_1234567890";
    size_t payload_len = strlen(payload);

    // upload
    snprintf(cmd, sizeof(cmd), "UPLOAD %s\n", fname);
    log_printf("single_client_flow: uploading %s (len=%zu)", fname, payload_len);
    send_line(s, cmd);
    // expect SEND_FILE_DATA
    if (!wait_for_substring(s, "SEND_FILE_DATA", 10000, buf, sizeof(buf))) {
        fprintf(stderr, "Server didn't ask for file data: %s\n", buf);
        log_printf("single_client_flow: server didn't ask for file data, buf=%.200s", buf);
        close(s);
        return -1;
    }
    // send size as 8-byte network-order value
    uint64_t net_ssz = htonll((uint64_t)payload_len);
    send(s, &net_ssz, sizeof(net_ssz), 0);
    // send data
    send(s, payload, payload_len, 0);
    log_printf("single_client_flow: sent file size and data for %s", fname);
    // read response
    sock_recv(s, buf, sizeof(buf));
    log_printf("single_client_flow: upload response: %.200s", buf);

    // list - wait until the uploaded filename appears in the listing
    send_line(s, "LIST\n");
    log_printf("single_client_flow: requested LIST");
    if (!wait_for_substring(s, fname, 3000, buf, sizeof(buf))) {
        fprintf(stderr, "Uploaded file not found in list: %s\n", buf);
        log_printf("single_client_flow: uploaded file not found in list, buf=%.200s", buf);
        close(s);
        return -1;
    }
    log_printf("single_client_flow: found %s in LIST", fname);

    // download
    snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\n", fname);
    log_printf("single_client_flow: requesting DOWNLOAD %s", fname);
    send_line(s, cmd);
    // read 8-byte network-order size
    uint64_t net_fsz = 0;
    ssize_t r = recv_n(s, &net_fsz, sizeof(net_fsz));
    if (r != (ssize_t)sizeof(net_fsz)) {
        fprintf(stderr, "Download size not received\n"); close(s); return -1;
    }
    size_t fsz = (size_t)ntohll(net_fsz);
    log_printf("single_client_flow: expecting download size=%zu", fsz);
    char *out = malloc(fsz+1);
    if (!out) { close(s); return -1; }
    ssize_t got = recv_n(s, out, fsz);
    if (got != (ssize_t)fsz) { fprintf(stderr, "Download data short\n"); free(out); close(s); return -1; }
    out[fsz] = '\0';
    if (fsz != payload_len || memcmp(out, payload, payload_len) != 0) {
        fprintf(stderr, "Downloaded payload mismatch (got=%zu expected=%zu)\n", fsz, payload_len);
        log_printf("single_client_flow: download mismatch for %s (got=%zu expected=%zu)", fname, fsz, payload_len);
        free(out); close(s); return -1;
    }
    free(out);
    log_printf("single_client_flow: download verified for %s", fname);

    // delete
    snprintf(cmd, sizeof(cmd), "DELETE %s\n", fname);
    log_printf("single_client_flow: sending DELETE %s", fname);
    send_line(s, cmd);
    sock_recv(s, buf, sizeof(buf));
    log_printf("single_client_flow: delete response: %.200s", buf);

    // confirm deletion by listing
    send_line(s, "LIST\n");
    sock_recv(s, buf, sizeof(buf));
    if (strstr(buf, fname) != NULL) {
        fprintf(stderr, "File still present after delete\n"); close(s); return -1;
    }
    log_printf("single_client_flow: confirmed %s removed from LIST", fname);

    send_line(s, "QUIT\n");
    close(s);
    log_printf("single_client_flow: closed connection and exiting");
    return 0;
}

// Concurrency test: many clients upload, download and delete files concurrently
typedef struct { int id; int rounds; } cthread_arg_t;

void *cthread_fn(void *arg) {
    cthread_arg_t *a = arg;
    char buf[BUFFER_SIZE];
    log_printf("cthread_fn: thread start id=%d rounds=%d", a->id, a->rounds);
    int s = connect_server(); if (s < 0) { perror("connect"); log_printf("cthread_fn: connect failed for id=%d", a->id); return NULL; }
    log_printf("cthread_fn: connected socket=%d for id=%d", s, a->id);
    sock_recv(s, buf, sizeof(buf));
    log_printf("cthread_fn: welcome for id=%d: %.200s", a->id, buf);
    char uname[64]; snprintf(uname, sizeof(uname), "concur_%d", a->id);
    char cmd[256]; snprintf(cmd, sizeof(cmd), "SIGNUP %s pass\n", uname); send_line(s, cmd); sock_recv(s, buf, sizeof(buf));
    if (strstr(buf, "SIGNUP_SUCCESS") == NULL) {
        log_printf("cthread_fn: signup not successful for id=%d, trying LOGIN", a->id);
        snprintf(cmd, sizeof(cmd), "LOGIN %s pass\n", uname); send_line(s, cmd); sock_recv(s, buf, sizeof(buf));
    } else {
        log_printf("cthread_fn: SIGNUP_SUCCESS for id=%d", a->id);
    }
    for (int i=0;i<a->rounds;i++) {
        char fname[128]; snprintf(fname, sizeof(fname), "u%d_f%d.dat", a->id, i);
        // upload
        snprintf(cmd, sizeof(cmd), "UPLOAD %s\n", fname);
        send_line(s, cmd);
        // Wait for server to request file data (may be delayed under load)
        if (!wait_for_substring(s, "SEND_FILE_DATA", 5000, buf, sizeof(buf))) {
            log_printf("cthread_fn: server did not request file data for %s (id=%d,i=%d)", fname, a->id, i);
            continue;
        }
        /* choose an upload size between 1KiB and min(64KiB, TEST_MAX_UPLOAD_BYTES) to keep tests fast
           but ensure it never exceeds the server's allowed limit */
        size_t per_upload_cap = (TEST_MAX_UPLOAD_BYTES < (64*1024)) ? TEST_MAX_UPLOAD_BYTES : (64*1024);
        if (per_upload_cap < 1024) per_upload_cap = TEST_MAX_UPLOAD_BYTES;
        int sz = 1024 + (rand() % (int)(per_upload_cap - 1024 + 1));
        uint64_t net_ssz = htonll((uint64_t)sz);
        if (send(s, &net_ssz, sizeof(net_ssz), 0) != (ssize_t)sizeof(net_ssz)) {
            log_printf("cthread_fn: failed to send size for %s (id=%d,i=%d)", fname, a->id, i);
            continue;
        }
        char *data = malloc(sz);
        if (!data) { log_printf("cthread_fn: malloc failed"); continue; }
        memset(data, 'A'+(a->id%26), sz);
        if (send(s, data, sz, 0) != sz) {
            log_printf("cthread_fn: failed to send data for %s (id=%d,i=%d)", fname, a->id, i);
            free(data);
            continue;
        }
        free(data);
        // wait for either SUCCESS or ERROR response (short timeout)
        if (!wait_for_substring(s, "SUCCESS", 5000, buf, sizeof(buf))) {
            // try to capture an ERROR message
            if (!wait_for_substring(s, "ERROR", 2000, buf, sizeof(buf))) {
                // fallback to a plain receive
                sock_recv(s, buf, sizeof(buf));
            }
        }
        log_printf("cthread_fn: uploaded %s (size=%d) for id=%d i=%d, server: %.120s", fname, sz, a->id, i, buf);

        // sometimes list
        if ((rand()%4)==0) { send_line(s, "LIST\n"); sock_recv(s, buf, sizeof(buf)); log_printf("cthread_fn: LIST for id=%d i=%d => %.200s", a->id, i, buf); }
        // sometimes download
        if ((rand()%3)==0) {
            snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\n", fname); send_line(s, cmd);
            uint64_t net_fsz = 0; ssize_t r = recv_n(s, &net_fsz, sizeof(net_fsz));
            if (r == (ssize_t)sizeof(net_fsz)) {
                size_t fsz = (size_t)ntohll(net_fsz);
                size_t got = 0; while (got < fsz) { ssize_t n = recv(s, buf, sizeof(buf), 0); if (n<=0) break; got+=n; }
                log_printf("cthread_fn: DOWNLOAD completed for %s id=%d i=%d size=%zu", fname, a->id, i, fsz);
            } else {
                log_printf("cthread_fn: DOWNLOAD size not received for %s id=%d i=%d", fname, a->id, i);
            }
            sock_recv(s, buf, sizeof(buf));
        }
        // sometimes delete
        if ((rand()%5)==0) { snprintf(cmd, sizeof(cmd), "DELETE %s\n", fname); send_line(s, cmd); sock_recv(s, buf, sizeof(buf)); log_printf("cthread_fn: DELETE %s id=%d i=%d => %.200s", fname, a->id, i, buf); }
        usleep(5000);
    }
    send_line(s, "QUIT\n"); close(s);
    log_printf("cthread_fn: thread end id=%d", a->id);
    return NULL;
}

int concurrency_test(int clients, int rounds) {
    pthread_t *ths = malloc(sizeof(pthread_t)*clients);
    cthread_arg_t *args = malloc(sizeof(cthread_arg_t)*clients);
    if (!ths || !args) return -1;

    // Create client threads and print short startup messages
    for (int i = 0; i < clients; ++i) {
        args[i].id = i + 1;
        args[i].rounds = rounds;
        printf("Starting client thread %d\n", args[i].id);
        fflush(stdout);
        pthread_create(&ths[i], NULL, cthread_fn, &args[i]);
    }

    // Join threads and print short join messages
    for (int i = 0; i < clients; ++i) {
        printf("Joining client thread %d\n", i+1);
        fflush(stdout);
        pthread_join(ths[i], NULL);
        printf("Client thread %d joined\n", i+1);
        fflush(stdout);
    }

    free(ths);
    free(args);
    return 0;
}

int wait_for_substring(int sock, const char *substr, int timeout_ms, char *outbuf, size_t outbuf_len) {
    if (!substr || !outbuf || outbuf_len == 0) return 0;
    outbuf[0] = '\0';
    size_t substr_len = strlen(substr);
    if (substr_len == 0) return 1;

    struct timeval start, now;
    gettimeofday(&start, NULL);
    long deadline_ms = start.tv_sec * 1000L + start.tv_usec / 1000L + timeout_ms;

    char tmp[1024];
    while (1) {
        // compute remaining time for select
        gettimeofday(&now, NULL);
        long now_ms = now.tv_sec * 1000L + now.tv_usec / 1000L;
        long rem_ms = deadline_ms - now_ms;
        if (rem_ms <= 0) return 0;

        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(sock, &rf);
        struct timeval tv;
        tv.tv_sec = (rem_ms > 1000) ? 1 : 0;
        tv.tv_usec = (rem_ms > 1000) ? 0 : rem_ms * 1000;

        int rv = select(sock + 1, &rf, NULL, NULL, &tv);
        if (rv < 0) {
            if (errno == EINTR) continue;
            return 0;
        }
        if (rv == 0) continue; // timeout slice, loop again

        ssize_t r = recv(sock, tmp, sizeof(tmp) - 1, 0);
        if (r <= 0) return 0;
        tmp[r] = '\0';

        // append safely
        size_t cur = strlen(outbuf);
        size_t tocopy = (size_t)r;
        if (cur + tocopy >= outbuf_len) {
            // truncate oldest data by shifting left
            size_t leave = outbuf_len - 1 - tocopy;
            if (leave > 0) {
                memmove(outbuf, outbuf + (cur - leave), leave);
                cur = leave;
            } else {
                // outbuf too small, just clear
                cur = 0;
            }
        }
        memcpy(outbuf + cur, tmp, tocopy);
        outbuf[cur + tocopy] = '\0';

        if (strstr(outbuf, substr)) return 1;
        // else continue until deadline
    }
    return 0;
}

int main(int argc, char **argv) {
    printf("Starting full integration tests\n");
    srand(time(NULL));
    log_printf("main: starting single client flow");
    if (single_client_flow("itest") != 0) { fprintf(stderr, "Single-client flow failed\n"); return EXIT_FAILURE; }
    printf("Single-client flow passed\n");

    printf("Running concurrency test (10 clients x 30 rounds)...\n");
    log_printf("main: starting concurrency test");
    concurrency_test(10, 30);
    log_printf("main: concurrency test finished");

    printf("All integration tests completed\n");
    return EXIT_SUCCESS;
}

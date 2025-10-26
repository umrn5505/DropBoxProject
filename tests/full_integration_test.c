#define _GNU_SOURCE
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

// forward declarations
static int wait_for_substring(int sock, const char *substr, int timeout_ms, char *outbuf, size_t outbuf_len);

#define SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 8080
#define BUFFER_SIZE 16384

static int get_server_port() {
    (void)0; // always use DEFAULT_SERVER_PORT
    return DEFAULT_SERVER_PORT;
}

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
    // set reasonable socket timeouts so client threads don't block forever
    struct timeval tv;
    tv.tv_sec = 10; tv.tv_usec = 0; // 10 second timeout
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
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

    // read welcome
    sock_recv(s, buf, sizeof(buf));

    char username[128];
    snprintf(username, sizeof(username), "%s_%ld", username_prefix, time(NULL) % 100000);
    char cmd[512];

    snprintf(cmd, sizeof(cmd), "SIGNUP %s pass\n", username);
    send_line(s, cmd);
    // Wait for either SIGNUP_SUCCESS or LOGIN_SUCCESS (signup may return immediate success)
    if (!wait_for_substring(s, "SIGNUP_SUCCESS", 3000, buf, sizeof(buf))) {
        // Not found - try LOGIN and wait for LOGIN_SUCCESS
        snprintf(cmd, sizeof(cmd), "LOGIN %s pass\n", username);
        send_line(s, cmd);
        if (!wait_for_substring(s, "LOGIN_SUCCESS", 3000, buf, sizeof(buf))) {
            fprintf(stderr, "Auth failed for %s: %s\n", username, buf);
            close(s);
            return -1;
        }
    }

    // prepare data
    const char *fname = "integr_test.bin";
    const char *payload = "THIS_IS_TEST_PAYLOAD_1234567890";
    size_t payload_len = strlen(payload);

    // upload
    snprintf(cmd, sizeof(cmd), "UPLOAD %s\n", fname);
    send_line(s, cmd);
    // expect SEND_FILE_DATA
    if (!wait_for_substring(s, "SEND_FILE_DATA", 10000, buf, sizeof(buf))) {
        fprintf(stderr, "Server didn't ask for file data: %s\n", buf);
        close(s);
        return -1;
    }
    // send size
    size_t ssz = payload_len;
    send(s, &ssz, sizeof(size_t), 0);
    // send data
    send(s, payload, payload_len, 0);
    // read response
    sock_recv(s, buf, sizeof(buf));

    // list - wait until the uploaded filename appears in the listing
    send_line(s, "LIST\n");
    if (!wait_for_substring(s, fname, 3000, buf, sizeof(buf))) {
        fprintf(stderr, "Uploaded file not found in list: %s\n", buf);
        close(s);
        return -1;
    }

    // download
    snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\n", fname);
    send_line(s, cmd);
    // read size
    ssize_t r = recv(s, buf, sizeof(size_t), 0);
    if (r != (ssize_t)sizeof(size_t)) {
        fprintf(stderr, "Download size not received\n"); close(s); return -1;
    }
    size_t fsz = *((size_t*)buf);
    char *out = malloc(fsz+1);
    if (!out) { close(s); return -1; }
    ssize_t got = recv_n(s, out, fsz);
    if (got != (ssize_t)fsz) { fprintf(stderr, "Download data short\n"); free(out); close(s); return -1; }
    out[fsz] = '\0';
    if (fsz != payload_len || memcmp(out, payload, payload_len) != 0) {
        fprintf(stderr, "Downloaded payload mismatch (got=%zu expected=%zu)\n", fsz, payload_len);
        free(out); close(s); return -1;
    }
    free(out);

    // delete
    snprintf(cmd, sizeof(cmd), "DELETE %s\n", fname);
    send_line(s, cmd);
    sock_recv(s, buf, sizeof(buf));

    // confirm deletion by listing
    send_line(s, "LIST\n");
    sock_recv(s, buf, sizeof(buf));
    if (strstr(buf, fname) != NULL) {
        fprintf(stderr, "File still present after delete\n"); close(s); return -1;
    }

    send_line(s, "QUIT\n");
    close(s);
    return 0;
}

// Concurrency test: many clients upload, download and delete files concurrently
typedef struct { int id; int rounds; } cthread_arg_t;

void *cthread_fn(void *arg) {
    cthread_arg_t *a = arg;
    char buf[BUFFER_SIZE];
    int s = connect_server(); if (s < 0) { perror("connect"); return NULL; }
    sock_recv(s, buf, sizeof(buf));
    char uname[64]; snprintf(uname, sizeof(uname), "concur_%d", a->id);
    char cmd[256]; snprintf(cmd, sizeof(cmd), "SIGNUP %s pass\n", uname); send_line(s, cmd); sock_recv(s, buf, sizeof(buf));
    if (strstr(buf, "SIGNUP_SUCCESS") == NULL) { snprintf(cmd, sizeof(cmd), "LOGIN %s pass\n", uname); send_line(s, cmd); sock_recv(s, buf, sizeof(buf)); }
    for (int i=0;i<a->rounds;i++) {
        char fname[128]; snprintf(fname, sizeof(fname), "u%d_f%d.dat", a->id, i);
        // upload
        snprintf(cmd, sizeof(cmd), "UPLOAD %s\n", fname); send_line(s, cmd);
        sock_recv(s, buf, sizeof(buf)); if (strstr(buf, "SEND_FILE_DATA") == NULL) continue;
        int sz = 256 + (rand()%512);
        size_t ssz = sz; send(s, &ssz, sizeof(size_t), 0);
        char *data = malloc(sz); memset(data, 'A'+(a->id%26), sz);
        send(s, data, sz, 0); free(data);
        sock_recv(s, buf, sizeof(buf));
        // sometimes list
        if ((rand()%4)==0) { send_line(s, "LIST\n"); sock_recv(s, buf, sizeof(buf)); }
        // sometimes download
        if ((rand()%3)==0) {
            snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\n", fname); send_line(s, cmd);
            ssize_t r = recv(s, buf, sizeof(size_t), 0);
            if (r == (ssize_t)sizeof(size_t)) {
                size_t fsz = *((size_t*)buf);
                size_t got = 0; while (got < fsz) { ssize_t n = recv(s, buf, sizeof(buf), 0); if (n<=0) break; got+=n; }
            }
            sock_recv(s, buf, sizeof(buf));
        }
        // sometimes delete
        if ((rand()%5)==0) { snprintf(cmd, sizeof(cmd), "DELETE %s\n", fname); send_line(s, cmd); sock_recv(s, buf, sizeof(buf)); }
        usleep(5000);
    }
    send_line(s, "QUIT\n"); close(s);
    return NULL;
}

int concurrency_test(int clients, int rounds) {
    pthread_t *ths = malloc(sizeof(pthread_t)*clients);
    cthread_arg_t *args = malloc(sizeof(cthread_arg_t)*clients);
    if (!ths || !args) {
        fprintf(stderr, "Failed to allocate thread arrays\n");
        return -1;
    }

    // Start client threads
    for (int i = 0; i < clients; ++i) {
        args[i].id = i + 1;
        args[i].rounds = rounds;
        fprintf(stderr, "Starting client thread %d\n", i+1);
        if (pthread_create(&ths[i], NULL, cthread_fn, &args[i]) != 0) {
            perror("pthread_create");
            ths[i] = 0;
        }
    }

    // Wait for client threads to finish (no cancellation/time limits)
    for (int i = 0; i < clients; ++i) {
        if (ths[i] == 0) continue;
        fprintf(stderr, "Joining client thread %d\n", i+1);
        pthread_join(ths[i], NULL);
        fprintf(stderr, "Client thread %d joined\n", i+1);
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
    if (single_client_flow("itest") != 0) { fprintf(stderr, "Single-client flow failed\n"); return EXIT_FAILURE; }
    printf("Single-client flow passed\n");

    printf("Running concurrency test (10 clients x 30 rounds)...\n");
    concurrency_test(10, 30);
    printf("Concurrency test finished\n");

    printf("All integration tests completed\n");
    return EXIT_SUCCESS;
}

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
#include <stdint.h>
#include <stdarg.h>

// Fast, lightweight integration test for DropBoxServer
// - Small uploads (1KB-4KB)
// - Few clients and rounds (configurable)
// - Short timeouts to fail fast
// Usage: ./fast_integration_test [clients] [rounds]

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8080
#define BUF 8192

static uint64_t htonll_u64(uint64_t x) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return ((uint64_t)htonl((uint32_t)(x & 0xffffffffu)) << 32) | (uint64_t)htonl((uint32_t)(x >> 32));
#else
    return x;
#endif
}
static uint64_t ntohll_u64(uint64_t x) { return htonll_u64(x); }

static int connect_server(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);
    if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(s); return -1; }
    return s;
}

static ssize_t recv_n(int sock, void *buf, size_t n) {
    size_t got = 0; char *p = buf;
    while (got < n) {
        ssize_t r = recv(sock, p + got, n - got, 0);
        if (r <= 0) return r;
        got += r;
    }
    return (ssize_t)got;
}

// wait_for_substring with short timeout
static int wait_for_substring(int sock, const char *sub, int timeout_ms, char *out, size_t outlen) {
    if (!sub || !out) return 0; out[0]='\0';
    long deadline = (long)time(NULL)*1000 + timeout_ms;
    char tmp[1024];
    while (1) {
        struct timeval tv = { .tv_sec = 0, .tv_usec = 200*1000 };
        fd_set rf; FD_ZERO(&rf); FD_SET(sock, &rf);
        int rv = select(sock+1, &rf, NULL, NULL, &tv);
        if (rv < 0) return 0;
        if (rv == 0) {
            if ((long)time(NULL)*1000 >= deadline) return 0; else continue;
        }
        ssize_t r = recv(sock, tmp, sizeof(tmp)-1, 0);
        if (r <= 0) return 0; tmp[r]='\0';
        // append
        size_t cur = strlen(out); size_t tocopy = (size_t)r;
        if (cur + tocopy >= outlen) {
            // keep last part
            size_t leave = outlen - 1 - tocopy;
            if (leave > 0) { memmove(out, out + (cur - leave), leave); cur = leave; } else cur = 0;
        }
        memcpy(out + cur, tmp, tocopy); out[cur+tocopy] = '\0';
        if (strstr(out, sub)) return 1;
    }
}

// Simple thread-safe logger with timestamp and thread id
static pthread_mutex_t __log_mtx = PTHREAD_MUTEX_INITIALIZER;
static void log_printf(const char *fmt, ...) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    char tbuf[32];
    int ms = tv.tv_usec / 1000;
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm);

    va_list ap;
    va_start(ap, fmt);
    pthread_mutex_lock(&__log_mtx);
    fprintf(stdout, "%s.%03d [tid=%lu] ", tbuf, ms, (unsigned long)pthread_self());
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
    fflush(stdout);
    pthread_mutex_unlock(&__log_mtx);
    va_end(ap);
}

// Single client simple flow (signup, upload small file, list, download, delete)
static int single_flow(const char *userprefix) {
    char buf[BUF];
    int s = connect_server(); if (s<0) { perror("connect"); return -1; }
    log_printf("single_flow: connected to server socket=%d", s);
    // read welcome
    recv(s, buf, sizeof(buf)-1, 0);
    log_printf("single_flow: welcome: %.200s", buf);
    char uname[64]; snprintf(uname, sizeof(uname), "%s_%u", userprefix, (unsigned) (rand()%100000));
    char cmd[256]; snprintf(cmd, sizeof(cmd), "SIGNUP %s pass\n", uname);
    send(s, cmd, strlen(cmd), 0);
    // wait for SIGNUP_SUCCESS or LOGIN_SUCCESS
    if (!wait_for_substring(s, "SIGNUP_SUCCESS", 1500, buf, sizeof(buf))) {
        snprintf(cmd, sizeof(cmd), "LOGIN %s pass\n", uname); send(s, cmd, strlen(cmd), 0);
        if (!wait_for_substring(s, "LOGIN_SUCCESS", 1500, buf, sizeof(buf))) { close(s); return -1; }
        log_printf("single_flow: LOGIN_SUCCESS for %s", uname);
    } else {
        log_printf("single_flow: SIGNUP_SUCCESS for %s", uname);
    }
    // small upload 2KB
    const char *fname = "fast_test.bin";
    snprintf(cmd, sizeof(cmd), "UPLOAD %s\n", fname); send(s, cmd, strlen(cmd), 0);
    if (!wait_for_substring(s, "SEND_FILE_DATA", 1500, buf, sizeof(buf))) { log_printf("single_flow: no SEND_FILE_DATA for %s", fname); close(s); return -1; }
    size_t sz = 2048;
    uint64_t net = htonll_u64((uint64_t)sz); send(s, &net, sizeof(net), 0);
    char *data = malloc(sz); memset(data, 'X', sz); send(s, data, sz, 0); free(data);
    log_printf("single_flow: sent %zu bytes for %s", sz, fname);
    // wait success
    if (wait_for_substring(s, "SUCCESS", 2000, buf, sizeof(buf))) log_printf("single_flow: upload success: %.200s", buf);
    else log_printf("single_flow: upload response not received in time");
    // list
    send(s, "LIST\n", 5, 0);
    if (wait_for_substring(s, fname, 1500, buf, sizeof(buf))) log_printf("single_flow: found %s in LIST", fname);
    else log_printf("single_flow: %s not present in LIST", fname);
    // download
    snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\n", fname); send(s, cmd, strlen(cmd), 0);
    uint64_t netfsz=0; if (recv_n(s, &netfsz, sizeof(netfsz))!= (ssize_t)sizeof(netfsz)) { log_printf("single_flow: download size not received"); close(s); return -1; }
    size_t fsz = (size_t) ntohll_u64(netfsz);
    log_printf("single_flow: download size=%zu", fsz);
    char *out = malloc(fsz+1); if (!out) { close(s); return -1; }
    if (recv_n(s, out, fsz) != (ssize_t)fsz) { free(out); close(s); return -1; }
    free(out);
    // delete
    snprintf(cmd, sizeof(cmd), "DELETE %s\n", fname); send(s, cmd, strlen(cmd), 0);
    log_printf("single_flow: sent DELETE for %s", fname);
    // quit
    send(s, "QUIT\n", 5, 0);
    close(s);
    log_printf("single_flow: finished for %s", uname);
    return 0;
}

// concurrency client thread args
typedef struct { int id; int rounds; } arg_t;

void *client_thread(void *v) {
    arg_t *a = v;
    char buf[BUF];
    log_printf("client_thread: start id=%d rounds=%d", a->id, a->rounds);
    int s = connect_server(); if (s<0) { log_printf("client_thread: connect failed id=%d", a->id); return NULL; }
    log_printf("client_thread: connected socket=%d id=%d", s, a->id);
    recv(s, buf, sizeof(buf)-1, 0); // welcome
    log_printf("client_thread: welcome: %.120s", buf);
    char uname[64]; snprintf(uname, sizeof(uname), "fast_%d_%u", a->id, (unsigned)(rand()%100000));
    char cmd[256]; snprintf(cmd, sizeof(cmd), "SIGNUP %s pass\n", uname); send(s, cmd, strlen(cmd), 0);
    if (wait_for_substring(s, "SUCCESS", 1000, buf, sizeof(buf))) log_printf("client_thread: signup/login success id=%d user=%s", a->id, uname);
    else log_printf("client_thread: signup/login did not return success id=%d", a->id);
    for (int i=0;i<a->rounds;i++) {
        char fname[64]; snprintf(fname, sizeof(fname), "f_%d_%d.dat", a->id, i);
        snprintf(cmd, sizeof(cmd), "UPLOAD %s\n", fname); send(s, cmd, strlen(cmd), 0);
        if (!wait_for_substring(s, "SEND_FILE_DATA", 1500, buf, sizeof(buf))) { log_printf("client_thread: no SEND_FILE_DATA for %s id=%d i=%d", fname, a->id, i); continue; }
        int sz = 1024 + (rand()%3072); // 1KB-4KB
        uint64_t net = htonll_u64((uint64_t)sz); send(s, &net, sizeof(net), 0);
        char *d = malloc(sz); memset(d, 'a'+(a->id%26), sz); send(s, d, sz, 0); free(d);
        if (wait_for_substring(s, "SUCCESS", 1500, buf, sizeof(buf))) log_printf("client_thread: uploaded %s size=%d id=%d i=%d", fname, sz, a->id, i);
        else log_printf("client_thread: upload response not received id=%d i=%d", a->id, i);
        // small sleep
        usleep(5000);
    }
    send(s, "QUIT\n", 5, 0); close(s); log_printf("client_thread: end id=%d", a->id); return NULL;
}

int main(int argc, char **argv) {
    int clients = 4, rounds = 5;
    if (argc >= 2) clients = atoi(argv[1]);
    if (argc >= 3) rounds = atoi(argv[2]);
    srand((unsigned)time(NULL));
    printf("Fast integration test: %d clients x %d rounds\n", clients, rounds);
    // quick single flow
    if (single_flow("itest_fast") != 0) { fprintf(stderr, "Single flow failed\n"); return 1; }
    pthread_t *ths = malloc(sizeof(pthread_t)*clients);
    arg_t *args = malloc(sizeof(arg_t)*clients);
    for (int i=0;i<clients;i++) { args[i].id = i+1; args[i].rounds = rounds; pthread_create(&ths[i], NULL, client_thread, &args[i]); }
    for (int i=0;i<clients;i++) pthread_join(ths[i], NULL);
    free(ths); free(args);
    printf("Fast test completed\n");
    return 0;
}

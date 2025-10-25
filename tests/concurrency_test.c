#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"
#define DEFAULT_SERVER_PORT 8080
#define BUFFER_SIZE 8192

static int get_server_port() {
    (void)0; // always use DEFAULT_SERVER_PORT
    return DEFAULT_SERVER_PORT;
}

typedef struct {
    int id;
    int ops;
} client_arg_t;

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

static void send_line(int sock, const char *line) {
    send(sock, line, strlen(line), 0);
}

void *client_thread(void *arg) {
    client_arg_t *ca = (client_arg_t*)arg;
    char buf[BUFFER_SIZE];
    int sock = connect_server();
    if (sock < 0) { perror("connect"); return NULL; }

    // read welcome and prompt
    sock_recv(sock, buf, sizeof(buf));
    // send SIGNUP
    char username[64];
    snprintf(username, sizeof(username), "ctuser_%d", ca->id);
    char signup_cmd[128];
    snprintf(signup_cmd, sizeof(signup_cmd), "SIGNUP %s pass\n", username);
    send_line(sock, signup_cmd);
    sock_recv(sock, buf, sizeof(buf));

    // If signup failed because user exists, try LOGIN
    if (strstr(buf, "SIGNUP_SUCCESS") == NULL && strstr(buf, "LOGIN_SUCCESS") == NULL) {
        char login_cmd[128];
        snprintf(login_cmd, sizeof(login_cmd), "LOGIN %s pass\n", username);
        send_line(sock, login_cmd);
        sock_recv(sock, buf, sizeof(buf));
    }

    // perform ops
    for (int i = 0; i < ca->ops; ++i) {
        int action = rand() % 4; // 0: upload,1:list,2:download,3:delete
        if (action == 0) {
            // upload small random data
            char fname[64]; snprintf(fname, sizeof(fname), "file_%d.txt", i);
            char cmd[128]; snprintf(cmd, sizeof(cmd), "UPLOAD %s\n", fname);
            send_line(sock, cmd);
            // wait for SEND_FILE_DATA (read server response)
            ssize_t r = sock_recv(sock, buf, sizeof(buf));
            if (r <= 0) break;
            if (strstr(buf, "SEND_FILE_DATA") == NULL) {
                // server didn't ask for data, continue
                continue;
            }
            // prepare data
            int sz = 64 + (rand() % 512);
            char *data = malloc(sz);
            for (int k = 0; k < sz; ++k) data[k] = 'A' + (rand()%26);
            // send size as size_t
            size_t ssz = (size_t)sz;
            send(sock, &ssz, sizeof(size_t), 0);
            // send data
            ssize_t sent = 0;
            while (sent < sz) {
                ssize_t w = send(sock, data + sent, sz - sent, 0);
                if (w <= 0) break;
                sent += w;
            }
            free(data);
            // read server response
            sock_recv(sock, buf, sizeof(buf));
        } else if (action == 1) {
            send_line(sock, "LIST\n");
            sock_recv(sock, buf, sizeof(buf));
        } else if (action == 2) {
            char fname[64]; snprintf(fname, sizeof(fname), "file_%d.txt", rand()% (ca->ops));
            char cmd[128]; snprintf(cmd, sizeof(cmd), "DOWNLOAD %s\n", fname);
            send_line(sock, cmd);
            // server should send size then data; first read raw data
            ssize_t r = recv(sock, buf, sizeof(size_t), 0);
            if (r == (ssize_t)sizeof(size_t)) {
                size_t fsz = *((size_t*)buf);
                // read fsz bytes (may be 0 or large)
                size_t got = 0;
                while (got < fsz) {
                    ssize_t n = recv(sock, buf, sizeof(buf), 0);
                    if (n <= 0) break;
                    got += n;
                }
            }
            // read textual response maybe
            sock_recv(sock, buf, sizeof(buf));
        } else { // delete
            char fname[64]; snprintf(fname, sizeof(fname), "file_%d.txt", rand()% (ca->ops));
            char cmd[128]; snprintf(cmd, sizeof(cmd), "DELETE %s\n", fname);
            send_line(sock, cmd);
            sock_recv(sock, buf, sizeof(buf));
        }
        // small pause
        usleep(10000);
    }

    send_line(sock, "QUIT\n");
    close(sock);
    return NULL;
}

int main(int argc, char **argv) {
    int clients = 20;
    int ops = 40;
    if (argc >= 2) clients = atoi(argv[1]);
    if (argc >= 3) ops = atoi(argv[2]);
    srand(time(NULL));

    pthread_t *ths = malloc(sizeof(pthread_t)*clients);
    client_arg_t *args = malloc(sizeof(client_arg_t)*clients);
    for (int i=0;i<clients;i++){
        args[i].id = i+1; args[i].ops = ops;
        pthread_create(&ths[i], NULL, client_thread, &args[i]);
    }
    for (int i=0;i<clients;i++) pthread_join(ths[i], NULL);
    free(ths); free(args);
    printf("All clients completed\n");
    return 0;
}


// Compile: gcc -o server server.c -lpthread

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define BACKLOG 5
#define BUF_SIZE 256

typedef struct {
    int sock;
    int id; // 0 = white, 1 = black
} client_t;

int clients_ready = 0;
client_t clients[2];
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void *client_thread(void *arg) {
    client_t *c = (client_t*)arg;
    char buf[BUF_SIZE];
    // wait until both clients are connected
    while (1) {
        pthread_mutex_lock(&lock);
        if (clients_ready == 2) { pthread_mutex_unlock(&lock); break; }
        pthread_mutex_unlock(&lock);
        usleep(100000);
    }

    // tell client its color
    if (c->id == 0) {
        send(c->sock, "START WHITE\n", 12, 0);
    } else {
        send(c->sock, "START BLACK\n", 12, 0);
    }

    // notify white that it's their turn
    if (c->id == 0) send(c->sock, "YOURTURN\n", 9, 0);

    // main loop: receive messages and forward to the other client
    while (1) {
        ssize_t r = recv(c->sock, buf, sizeof(buf)-1, 0);
        if (r <= 0) {
            // client disconnected
            int other = 1 - c->id;
            if (clients[other].sock != 0) {
                send(clients[other].sock, "OPPONENT_DISCONNECTED\n", 22, 0);
            }
            close(c->sock);
            c->sock = 0;
            return NULL;
        }
        buf[r] = 0;

        // simple commands: MOVE <move>\n  or RESIGN\n or CHAT <text>\n
        // forward to other client and manage turn tokens
        int other = 1 - c->id;

        // If move, forward and give other the YOURTURN message
        if (strncmp(buf, "MOVE ", 5) == 0) {
            // forward move
            if (clients[other].sock) {
                send(clients[other].sock, buf, strlen(buf), 0);
                // tell the other it's their turn
                send(clients[other].sock, "YOURTURN\n", 9, 0);
            }
        } else if (strncmp(buf, "RESIGN", 6) == 0) {
            if (clients[other].sock) {
                send(clients[other].sock, "OPPONENT_RESIGNED\n", 18, 0);
            }
        } else {
            // forward anything else (e.g., CHAT)
            if (clients[other].sock) send(clients[other].sock, buf, strlen(buf), 0);
        }
    }

    return NULL;
}

int main(int argc, char **argv) {
    int port = 5000;
    if (argc >= 2) port = atoi(argv[1]);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); exit(1); }

    int on = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen"); exit(1);
    }

    printf("Server listening on port %d. Waiting for 2 players...\n", port);

    for (int i=0;i<2;i++) clients[i].sock = 0;

    int connected = 0;
    while (connected < 2) {
        struct sockaddr_in cliaddr;
        socklen_t clilen = sizeof(cliaddr);
        int clientfd = accept(listen_fd, (struct sockaddr*)&cliaddr, &clilen);
        if (clientfd < 0) { perror("accept"); continue; }

        clients[connected].sock = clientfd;
        clients[connected].id = connected;
        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, &clients[connected]);
        pthread_detach(tid);

        connected++;
        pthread_mutex_lock(&lock);
        clients_ready = connected;
        pthread_mutex_unlock(&lock);

        printf("Player %d connected (fd=%d)\n", connected, clientfd);
    }

    // server main thread just waits while client threads run
    while (1) {
        sleep(10);
    }

    close(listen_fd);
    return 0;
}

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>

#define MAX_CLIENTS 256
#define PORT 8080
#define BUFF_SIZE 4096

typedef enum {
    STATE_NEW,
    STATE_CONNECTED,
    STATE_DISCONNECTED,
} state_e;

typedef struct {
    int fd;
    state_e state;
    char buffer[4096];
} clientstate_t;

clientstate_t clientStates[MAX_CLIENTS];

void init_clients() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        clientStates[i].fd    = -1; // is indicates a free slot
        clientStates[i].state = STATE_NEW;
        memset(&clientStates[i].buffer, 0, BUFF_SIZE);
    }
}

int find_free_slot() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientStates[i].fd == -1) {
            return i;
        }
    }
    return -1;
}

int main() {
    int listen_fd, conn_fd, nfds, freeSlot;
    struct sockaddr_in server_addr, client_addr;

    socklen_t client_len = sizeof(client_addr);

    fd_set read_fds, write_fds;

    init_clients();

    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port        = htons(PORT);

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind");
        exit(EXIT_FAILURE);
    }
    // max number of pending connections
    if (listen(listen_fd, 10) == -1) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d\n", PORT);

    while (1) {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        FD_SET(listen_fd, &read_fds);
        nfds = listen_fd + 1;

        // add active conections to the read set
        // edge case handling
        for (int i = 0; i < MAX_CLIENTS; i++) {
            // .fd != -1 means it is actively being used
            if (clientStates[i].fd != -1) {
                FD_SET(clientStates[i].fd, &read_fds);
                if (clientStates[i].fd >= nfds) {
                    nfds = clientStates[i].fd + 1;
                }
            }
        }

        // the return of select indicates the number of fds that is ready for read/write/disconnect operations
        // select always return when there is new connection because
        // 1. we have "bind" and "listen" to listen_fd,
        // 2. we have set FD_SET(listen_fd, &read_fds)
        // 3. in every loop we must be listening to listen_fd
        // 4. plus additonal that connected and set into clientStates[freeSlot]
        // 5. now we are listening multiple fds at the same time to avoid blocking, thereby achieving
        //    - waiting for multiple connection and
        //    - receiving message
        //    at the same time
        if (select(nfds, &read_fds, &write_fds, NULL, NULL) == -1) {
            perror("Select");
            exit(EXIT_FAILURE);
        }

        if (FD_ISSET(listen_fd, &read_fds)) {
            if ((conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len)) == -1) {
                perror("accpet");
                continue;
            }
            printf("New connection from %s:%d\n",
                inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port));

            freeSlot = find_free_slot();
            if (freeSlot == -1) {
                printf("Server full, closing new connection");
                close(conn_fd);
            } else {
                clientStates[freeSlot].fd    = conn_fd;
                clientStates[freeSlot].state = STATE_CONNECTED;
            }
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            clientstate_t* currstate = clientStates + i;
            if (currstate->fd != -1 &&
                FD_ISSET(currstate->fd, &read_fds)) {
                ssize_t bytes_read = read(
                    currstate->fd,
                    &currstate->buffer,
                    sizeof(currstate->buffer));

                if (bytes_read <= 0) {
                    close(currstate->fd);
                    currstate->fd    = -1;
                    currstate->state = STATE_DISCONNECTED;
                    printf("Client disconnected or error\n");
                } else {
                    printf("Received data from the client: %s\n", currstate->buffer);
                }
            }
        }
    }
}

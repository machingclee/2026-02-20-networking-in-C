#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

typedef enum {
    PROTO_HELLO,
} proto_type_e;

typedef struct {
    proto_type_e type;
    unsigned short len;
} proto_hdr_t;

void handle_client(int cfd) {
    char buf[4096] = { 0 };
    // declare a buffer and declare what type it is by pointer-casting, so that the pointer operation
    // will automatically jump to correct position according to the size of the casted type
    proto_hdr_t* hdr = (proto_hdr_t*)buf;

    hdr->type    = htonl(PROTO_HELLO);
    int real_len = sizeof(int);     // for computation
    hdr->len     = htons(real_len); // for network trasmission

    int* data = (int*)&hdr[1];
    *data     = htonl(1);

    write(cfd, hdr, sizeof(proto_hdr_t) + real_len);
}

int main() {
    struct sockaddr_in serverInfo = { 0 };
    struct sockaddr_in clientInfo = { 0 };
    int clientSize                = 0;
    serverInfo.sin_family         = AF_INET; // ipv4 socket

    // s_addr means differently in server side and client side, as it will be used differently
    // one use bind and listen, and one use connect, both are using the field `s_addr` differently
    serverInfo.sin_addr.s_addr = 0; // receive connection from which address, 0 mean any
    serverInfo.sin_port        = htons(5555);

    // 0 mean the default protocol
    // SOCKET_STREAM is equivalent to TCP
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        return -1;
    }
    if (bind(server_socket, (struct sockaddr*)&serverInfo, sizeof(serverInfo)) == -1) {
        perror("bind");
        close(server_socket);
        return -1;
    }
    int BACKLOG = 0; // maximum number of pending connections
    if (listen(server_socket, BACKLOG) == -1) {
    }

    // accept write to the pointer to tell where the connection comes from
    while (1) {
        // printf("Waiting for connection...\n");
        int client_fd = accept(server_socket, (struct sockaddr*)&clientInfo, (socklen_t*)&clientSize);
        printf("clientSize: %d\n", clientSize);

        if (client_fd == -1) {
            perror("accept");
            close(server_socket);
            return -1;
        }

        handle_client(client_fd);

        close(client_fd);
    }
}

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

typedef enum {
    PROTO_HELLO,
} proto_type_e;

typedef struct {
    proto_type_e type;
    unsigned short len;
} proto_hdr_t;

void handle_client(int fd) {
    char buf[4096] = { 0 };
    read(fd, buf, sizeof(proto_hdr_t) + sizeof(int));
    proto_hdr_t* hdr = (proto_hdr_t*)buf;
    hdr->type        = ntohl(hdr->type);
    hdr->len         = ntohs(hdr->len);

    int* data = (int*)&hdr[1];
    *data     = ntohl(*data);

    if (hdr->type != PROTO_HELLO) {
        printf("Protocol mismatch\n");
        return;
    }

    if (*data != 1) {
        printf("Version: %d \n", *data);
        printf("Protocal version mismatch\n");
        return;
    }

    printf("Server connected to protocol v1\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <IP_ADDRESS>\n", argv[0]);
        return -1;
    }

    struct sockaddr_in serverInfo = { 0 };
    int clientSize                = 0;
    serverInfo.sin_family         = AF_INET; // ipv4 socket
    serverInfo.sin_addr.s_addr    = inet_addr(argv[1]);
    serverInfo.sin_port           = htons(5555);

    // 0 mean the default protocol
    // SOCKET_STREAM is equivalent to TCP
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd == -1) {
        perror("socket");
        return -1;
    }

    if (connect(fd, (struct sockaddr*)&serverInfo, sizeof(serverInfo)) == -1) {
        perror("connect");
        close(fd);
        return 0;
    }

    handle_client(fd);

    close(fd);
}
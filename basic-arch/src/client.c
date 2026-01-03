#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#define MESSAGE "Hello from client!"
#define PORT "7471"

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    struct rdma_cm_id *id;
    struct rdma_addrinfo hints = {}, *res;
    struct ibv_mr *mr;
    struct ibv_wc wc;
    char buf[32] = {0};
    int ret;

    hints.ai_port_space = RDMA_PS_TCP;
    rdma_getaddrinfo(argv[1], PORT, &hints, &res);

    rdma_create_ep(&id, res, NULL, NULL);
    rdma_connect(id, NULL);

    // Register memory
    mr = rdma_reg_msgs(id, buf, sizeof(buf));

    // Send message
    strcpy(buf, MESSAGE);
    rdma_post_send(id, NULL, buf, strlen(MESSAGE) + 1, mr, 0);
    while (rdma_get_send_comp(id, &wc) <= 0);

    printf("Sent: %s\n", MESSAGE);

    // Post receive for response
    rdma_post_recv(id, NULL, buf, sizeof(buf), mr);
    while (rdma_get_recv_comp(id, &wc) <= 0);

    printf("Received: %s\n", buf);

    rdma_dereg_mr(mr);
    rdma_disconnect(id);
    rdma_destroy_ep(id);
    rdma_freeaddrinfo(res);

    return 0;
}
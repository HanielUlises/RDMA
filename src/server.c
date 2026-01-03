#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#define MESSAGE "Hello from server!"
#define PORT "7471"

int main(int argc, char **argv) {
    struct rdma_cm_id *listen_id, *id;
    struct rdma_addrinfo hints = {}, *res;
    struct ibv_mr *mr;
    struct ibv_wc wc;
    char buf[32] = {0};
    int ret;

    hints.ai_port_space = RDMA_PS_TCP;
    hints.ai_flags = RAI_PASSIVE;
    rdma_getaddrinfo(NULL, PORT, &hints, &res);

    rdma_create_ep(&listen_id, res, NULL, NULL);
    rdma_listen(listen_id, 0);

    printf("Server waiting for connection...\n");
    rdma_get_request(listen_id, &id);
    rdma_accept(id, NULL);

    // Register memory for receive
    mr = rdma_reg_msgs(id, buf, sizeof(buf));
    rdma_post_recv(id, NULL, buf, sizeof(buf), mr);

    // Wait for client message
    while (rdma_get_recv_comp(id, &wc) <= 0);
    printf("Received: %s\n", buf);

    // Send response
    strcpy(buf, MESSAGE);
    rdma_post_send(id, NULL, buf, strlen(MESSAGE) + 1, mr, 0);
    while (rdma_get_send_comp(id, &wc) <= 0);

    printf("Sent: %s\n", MESSAGE);

    rdma_dereg_mr(mr);
    rdma_disconnect(id);
    rdma_destroy_ep(id);
    rdma_destroy_ep(listen_id);
    rdma_freeaddrinfo(res);

    return 0;
}
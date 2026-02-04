#include <stdio.h>
#include <infiniband/verbs.h>

extern struct ibv_pd *pd;
extern struct ibv_mr *mr;

char send_buffer[] = "Hello, RDMA World!";

int main() {
    struct ibv_qp *qp;

    struct ibv_sge sge;
    sge.addr = (uintptr_t)send_buffer;
    sge.length = sizeof(send_buffer);
    sge.lkey = mr->lkey;

    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = 1;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;

    int ret = ibv_post_send(qp, &wr, &bad_wr);
    if (ret) {
        perror("ibv_post_send failed");
        return 1;
    }

    printf("RDMA Send request posted successfully.\n");

    return 0;
}
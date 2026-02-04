#include <stdio.h>
#include <infiniband/verbs.h>
#include <string.h>
#include <stdlib.h>

int main() {
    int num_devices;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        perror("ibv_get_device_list");
        exit(1);
    }
    if (num_devices == 0) {
        printf("No devices found\n");
        exit(1);
    }
    struct ibv_context *ctx = ibv_open_device(dev_list[0]);
    if (!ctx) {
        perror("ibv_open_device");
        exit(1);
    }
    ibv_free_device_list(dev_list);

    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    if (!pd) {
        perror("ibv_alloc_pd");
        exit(1);
    }

    char send_buffer[] = "Hello, RDMA World!";

    struct ibv_mr *mr = ibv_reg_mr(pd, send_buffer, sizeof(send_buffer), IBV_ACCESS_LOCAL_WRITE);
    if (!mr) {
        perror("ibv_reg_mr");
        exit(1);
    }

    struct ibv_cq *cq = ibv_create_cq(ctx, 16, NULL, NULL, 0);
    if (!cq) {
        perror("ibv_create_cq");
        exit(1);
    }

    struct ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.cap.max_send_wr = 16;
    qp_init_attr.cap.max_recv_wr = 16;
    qp_init_attr.cap.max_sg_sge = 1;
    qp_init_attr.cap.max_send_sge = 1;

    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) {
        perror("ibv_create_qp");
        exit(1);
    }

    struct ibv_port_attr port_attr;
    ibv_query_port(ctx, 1, &port_attr);

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = 1;
    attr.pkey_index = 0;
    attr.qp_access_flags = 0;
    int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    if (ibv_modify_qp(qp, &attr, flags)) {
        perror("ibv_modify_qp to INIT");
        exit(1);
    }

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_1024;
    attr.dest_qp_num = 0;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = port_attr.lid;
    attr.ah_attr.sl = 0;
    attr.ah_attr.src_path_bits = 0;
    attr.ah_attr.port_num = 1;
    flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
            IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    if (ibv_modify_qp(qp, &attr, flags)) {
        perror("ibv_modify_qp to RTR");
        exit(1);
    }

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 14;
    attr.retry_count = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;
    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_COUNT |
            IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    if (ibv_modify_qp(qp, &attr, flags)) {
        perror("ibv_modify_qp to RTS");
        exit(1);
    }

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
        exit(1);
    }

    struct ibv_wc wc;
    int done = 0;
    while (!done) {
        int num_comp = ibv_poll_cq(cq, 1, &wc);
        if (num_comp > 0) {
            if (wc.status != IBV_WC_SUCCESS) {
                printf("Failed status %s (%d) for wr_id %d\n",
                       ibv_wc_status_str(wc.status),
                       wc.status,
                       (int)wc.wr_id);
                exit(1);
            }
            printf("Completion for wr_id %d\n", (int)wc.wr_id);
            done = 1;
        }
    }

    printf("RDMA Send request posted and completed successfully.\n");

    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);

    return 0;
}
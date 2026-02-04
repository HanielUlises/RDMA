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
    struct ibv_mr *mr_send = ibv_reg_mr(pd, send_buffer, sizeof(send_buffer), 0);
    if (!mr_send) {
        perror("ibv_reg_mr send");
        exit(1);
    }

    char recv_buffer[sizeof(send_buffer)] = {0};
    struct ibv_mr *mr_recv = ibv_reg_mr(pd, recv_buffer, sizeof(recv_buffer), IBV_ACCESS_LOCAL_WRITE);
    if (!mr_recv) {
        perror("ibv_reg_mr recv");
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
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.cap.max_send_sge = 1;

    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) {
        perror("ibv_create_qp");
        exit(1);
    }

    uint32_t local_qpn = qp->qp_num;

    struct ibv_port_attr port_attr;
    if (ibv_query_port(ctx, 1, &port_attr)) {
        perror("ibv_query_port");
        exit(1);
    }
    uint16_t local_lid = port_attr.lid;

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
    attr.dest_qp_num = local_qpn;
    attr.rq_psn = 0;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = 12;
    attr.ah_attr.is_global = 0;
    attr.ah_attr.dlid = local_lid;
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
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.sq_psn = 0;
    attr.max_rd_atomic = 1;
    flags = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
            IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    if (ibv_modify_qp(qp, &attr, flags)) {
        perror("ibv_modify_qp to RTS");
        exit(1);
    }

    struct ibv_sge sge_recv;
    sge_recv.addr = (uintptr_t)recv_buffer;
    sge_recv.length = sizeof(recv_buffer);
    sge_recv.lkey = mr_recv->lkey;

    struct ibv_recv_wr wr_recv;
    struct ibv_recv_wr *bad_wr_recv;
    memset(&wr_recv, 0, sizeof(wr_recv));
    wr_recv.wr_id = 2;
    wr_recv.next = NULL;
    wr_recv.sg_list = &sge_recv;
    wr_recv.num_sge = 1;

    int ret = ibv_post_recv(qp, &wr_recv, &bad_wr_recv);
    if (ret) {
        perror("ibv_post_recv failed");
        exit(1);
    }

    struct ibv_sge sge_send;
    sge_send.addr = (uintptr_t)send_buffer;
    sge_send.length = sizeof(send_buffer);
    sge_send.lkey = mr_send->lkey;

    struct ibv_send_wr wr_send;
    struct ibv_send_wr *bad_wr_send;
    memset(&wr_send, 0, sizeof(wr_send));
    wr_send.wr_id = 1;
    wr_send.next = NULL;
    wr_send.sg_list = &sge_send;
    wr_send.num_sge = 1;
    wr_send.opcode = IBV_WR_SEND;
    wr_send.send_flags = IBV_SEND_SIGNALED;

    ret = ibv_post_send(qp, &wr_send, &bad_wr_send);
    if (ret) {
        perror("ibv_post_send failed");
        exit(1);
    }

    struct ibv_wc wc;
    int done_send = 0;
    int done_recv = 0;
    while (!done_send || !done_recv) {
        int num_comp = ibv_poll_cq(cq, 1, &wc);
        if (num_comp > 0) {
            if (wc.status != IBV_WC_SUCCESS) {
                printf("Failed status %s (%d) for wr_id %d\n",
                       ibv_wc_status_str(wc.status),
                       wc.status,
                       (int)wc.wr_id);
                exit(1);
            }
            if (wc.opcode == IBV_WC_SEND) {
                printf("Send completion for wr_id %d\n", (int)wc.wr_id);
                done_send = 1;
            } else if (wc.opcode == IBV_WC_RECV) {
                printf("Recv completion for wr_id %d\n", (int)wc.wr_id);
                printf("Received: %s\n", recv_buffer);
                done_recv = 1;
            }
        }
    }

    printf("RDMA Send and Receive completed successfully.\n");

    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr_recv);
    ibv_dereg_mr(mr_send);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);

    return 0;
}
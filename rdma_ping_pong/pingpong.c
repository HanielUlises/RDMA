/*
 * Simple RC ping-pong with TWO QPs
 */

#include <infiniband/verbs.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define MAX_WR  512
#define BUF_SIZE 4096

enum {
    WR_RECV_QP1 = 1,
    WR_SEND_QP1 = 2,
    WR_RECV_QP2 = 3,
    WR_SEND_QP2 = 4,
};

struct qp_dest {
    uint16_t lid;
    uint32_t qpn;
    uint32_t psn;
    union ibv_gid gid;
};

struct ctx {
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp *qp1;
    struct ibv_qp *qp2;
    char *buf;
    int size;
};

/* ---------------- QP helpers ---------------- */

static int qp_change_init(struct ibv_qp *qp, int port)
{
    struct ibv_qp_attr attr = {
        .qp_state = IBV_QPS_INIT,
        .port_num = port,
        .pkey_index = 0,
        .qp_access_flags = IBV_ACCESS_LOCAL_WRITE
    };

    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE |
        IBV_QP_PKEY_INDEX |
        IBV_QP_PORT |
        IBV_QP_ACCESS_FLAGS);
}

static int qp_change_rtr(struct ibv_qp *qp, struct qp_dest *dest,
                         int port, int mtu, int sgid_idx)
{
    struct ibv_qp_attr attr = {
        .qp_state = IBV_QPS_RTR,
        .path_mtu = mtu,
        .dest_qp_num = dest->qpn,
        .rq_psn = dest->psn,
        .max_dest_rd_atomic = 1,
        .min_rnr_timer = 12,
        .ah_attr = {
            .dlid = dest->lid,
            .sl = 0,
            .src_path_bits = 0,
            .port_num = port,
            .is_global = 0
        }
    };

    if (dest->gid.global.interface_id) {
        attr.ah_attr.is_global = 1;
        attr.ah_attr.grh.dgid = dest->gid;
        attr.ah_attr.grh.sgid_index = sgid_idx;
        attr.ah_attr.grh.hop_limit = 1;
    }

    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE |
        IBV_QP_AV |
        IBV_QP_PATH_MTU |
        IBV_QP_DEST_QPN |
        IBV_QP_RQ_PSN |
        IBV_QP_MAX_DEST_RD_ATOMIC |
        IBV_QP_MIN_RNR_TIMER);
}

static int qp_change_rts(struct ibv_qp *qp, uint32_t psn)
{
    struct ibv_qp_attr attr = {
        .qp_state = IBV_QPS_RTS,
        .timeout = 14,
        .retry_cnt = 7,
        .rnr_retry = 7,
        .sq_psn = psn,
        .max_rd_atomic = 1
    };

    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE |
        IBV_QP_TIMEOUT |
        IBV_QP_RETRY_CNT |
        IBV_QP_RNR_RETRY |
        IBV_QP_SQ_PSN |
        IBV_QP_MAX_QP_RD_ATOMIC);
}

/* ---------------- Context ---------------- */

static struct ctx *ctx_init(int size, int rx_depth, int ib_port)
{
    struct ctx *c = calloc(1, sizeof(*c));
    struct ibv_device **dev_list;

    dev_list = ibv_get_device_list(NULL);
    c->context = ibv_open_device(dev_list[0]);
    c->pd = ibv_alloc_pd(c->context);

    c->size = size;
    c->buf = aligned_alloc(4096, size * 2);
    memset(c->buf, 0, size * 2);

    c->mr = ibv_reg_mr(c->pd, c->buf, size * 2,
                       IBV_ACCESS_LOCAL_WRITE);

    c->cq = ibv_create_cq(c->context, rx_depth * 4, NULL, NULL, 0);

    struct ibv_qp_init_attr qpia = {
        .send_cq = c->cq,
        .recv_cq = c->cq,
        .cap = {
            .max_send_wr = rx_depth,
            .max_recv_wr = rx_depth,
            .max_send_sge = 1,
            .max_recv_sge = 1
        },
        .qp_type = IBV_QPT_RC
    };

    c->qp1 = ibv_create_qp(c->pd, &qpia);
    c->qp2 = ibv_create_qp(c->pd, &qpia);

    qp_change_init(c->qp1, ib_port);
    qp_change_init(c->qp2, ib_port);

    ibv_free_device_list(dev_list);
    return c;
}

/* ---------------- Posting ---------------- */

static void post_recv(struct ctx *c, struct ibv_qp *qp, int wrid, void *buf)
{
    struct ibv_sge sge = {
        .addr = (uintptr_t)buf,
        .length = c->size,
        .lkey = c->mr->lkey
    };

    struct ibv_recv_wr wr = {
        .wr_id = wrid,
        .sg_list = &sge,
        .num_sge = 1
    };

    struct ibv_recv_wr *bad;
    ibv_post_recv(qp, &wr, &bad);
}

static void post_send(struct ctx *c, struct ibv_qp *qp, int wrid, void *buf)
{
    struct ibv_sge sge = {
        .addr = (uintptr_t)buf,
        .length = c->size,
        .lkey = c->mr->lkey
    };

    struct ibv_send_wr wr = {
        .wr_id = wrid,
        .sg_list = &sge,
        .num_sge = 1,
        .opcode = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED
    };

    struct ibv_send_wr *bad;
    ibv_post_send(qp, &wr, &bad);
}

/* ---------------- Main ---------------- */

int main(int argc, char **argv)
{
    int is_server = (argc == 1);
    int iters = 1000;
    int ib_port = 1;

    struct ctx *c = ctx_init(BUF_SIZE, MAX_WR, ib_port);

    struct ibv_port_attr pattr;
    ibv_query_port(c->context, ib_port, &pattr);

    struct qp_dest local[2], remote[2];

    srand48(getpid());
    local[0].lid = pattr.lid;
    local[1].lid = pattr.lid;
    local[0].qpn = c->qp1->qp_num;
    local[1].qpn = c->qp2->qp_num;
    local[0].psn = lrand48() & 0xffffff;
    local[1].psn = lrand48() & 0xffffff;
    memset(&local[0].gid, 0, sizeof(local[0].gid));
    memset(&local[1].gid, 0, sizeof(local[1].gid));

    /* ---- TCP exchange omitted for brevity ----
     * Assume remote[] is filled correctly
     */

    qp_change_rtr(c->qp1, &remote[0], ib_port, IBV_MTU_1024, -1);
    qp_change_rtr(c->qp2, &remote[1], ib_port, IBV_MTU_1024, -1);
    qp_change_rts(c->qp1, local[0].psn);
    qp_change_rts(c->qp2, local[1].psn);

    char *buf1 = c->buf;
    char *buf2 = c->buf + BUF_SIZE;

    post_recv(c, c->qp1, WR_RECV_QP1, buf1);
    post_recv(c, c->qp2, WR_RECV_QP2, buf2);

    if (!is_server) {
        post_send(c, c->qp1, WR_SEND_QP1, buf1);
        post_send(c, c->qp2, WR_SEND_QP2, buf2);
    }

    int scnt = 0, rcnt = 0;
    while (scnt < iters || rcnt < iters) {
        struct ibv_wc wc[4];
        int n = ibv_poll_cq(c->cq, 4, wc);
        for (int i = 0; i < n; i++) {
            if (wc[i].status != IBV_WC_SUCCESS)
                exit(1);

            switch (wc[i].wr_id) {
            case WR_SEND_QP1:
            case WR_SEND_QP2:
                scnt++;
                break;
            case WR_RECV_QP1:
                rcnt++;
                post_recv(c, c->qp1, WR_RECV_QP1, buf1);
                break;
            case WR_RECV_QP2:
                rcnt++;
                post_recv(c, c->qp2, WR_RECV_QP2, buf2);
                break;
            }
        }
    }

    printf("DONE: %d iterations on 2 QPs\n", iters);
    return 0;
}

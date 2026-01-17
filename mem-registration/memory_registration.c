#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <infiniband/verbs.h>

#define BUFFER_SIZE 4096

int main()
{
    struct ibv_device **dev_list = NULL;
    struct ibv_device *ib_dev = NULL;
    struct ibv_context *context = NULL;
    struct ibv_pd *pd = NULL;
    struct ibv_mr *mr = NULL;

    char *buffer = NULL;
    int num_devices = 0;

    dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list || num_devices == 0) {
        perror("Failed to get IB devices list");
        return 1;
    }

    ib_dev = dev_list[0];
    printf("Using RDMA device: %s\n", ibv_get_device_name(ib_dev));

    context = ibv_open_device(ib_dev);
    if (!context) {
        perror("Failed to open device");
        goto cleanup_dev_list;
    }

    pd = ibv_alloc_pd(context);
    if (!pd) {
        perror("Failed to allocate PD");
        goto cleanup_context;
    }

    buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        perror("Failed to allocate buffer");
        goto cleanup_pd;
    }
    memset(buffer, 0, BUFFER_SIZE);

    mr = ibv_reg_mr(
        pd,
        buffer,
        BUFFER_SIZE,
        IBV_ACCESS_LOCAL_WRITE |
        IBV_ACCESS_REMOTE_READ |
        IBV_ACCESS_REMOTE_WRITE
    );

    if (!mr) {
        perror("Failed to register MR");
        goto cleanup_buffer;
    }

    /* 7. Print MR information */
    printf("Memory Region registered successfully\n");
    printf("  Address : %p\n", mr->addr);
    printf("  Length  : %zu\n", mr->length);
    printf("  LKey    : 0x%x\n", mr->lkey);
    printf("  RKey    : 0x%x\n", mr->rkey);

    /*
     * At this point, the MR can be used in:
     * - Send/Recv
     * - RDMA Read
     * - RDMA Write
     *
     * Make sure NO outstanding WRs exist before deregistering.
     */

    /* 8. Deregister Memory Region */
    if (ibv_dereg_mr(mr)) {
        perror("Failed to deregister MR");
    } else {
        printf("Memory Region deregistered\n");
    }

cleanup_buffer:
    free(buffer);

cleanup_pd:
    ibv_dealloc_pd(pd);

cleanup_context:
    ibv_close_device(context);

cleanup_dev_list:
    ibv_free_device_list(dev_list);

    return 0;
}

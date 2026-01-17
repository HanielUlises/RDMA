#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <infiniband/verbs.h>

#include <cuda_runtime.h>
#define size_t int 

int main() {
  struct ibv_device **dev_list;
  struct ibv_context *ctx;
  struct ibv_pd *pd;
  struct ibv_mr *mr;
  void *buf;
  size_t size = 4096;
  int num_devices;
  dev_list = ibv_get_device_list(&num_devices);
  if (!dev_list) {
    exit(1);
  }
  if (num_devices == 0) {
    exit(1);
  }
  ctx = ibv_open_device(dev_list[0]);
  if (!ctx) {
    exit(1);
  }
  pd = ibv_alloc_pd(ctx);
  if (!pd) {
    exit(1);
  }
  cudaMalloc(&buf, size);
  mr = ibv_reg_mr(pd, buf, size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
  if (!mr) {
    exit(1);
  }
  printf("rkey %u\n", mr->rkey);
  printf("lkey %u\n", mr->lkey);
  printf("addr %p\n", mr->addr);
  printf("length %zu\n", mr->length);
  ibv_dereg_mr(mr);
  cudaFree(buf);
  ibv_dealloc_pd(pd);
  ibv_close_device(ctx);
  ibv_free_device_list(dev_list);
  return 0;

}
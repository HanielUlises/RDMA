cmake_minimum_required(VERSION 3.10)
project(rdma_example LANGUAGES C)

find_library(IBVERBS_LIB ibverbs)
find_library(RDMACM_LIB rdmacm)

add_executable(server src/server.c)
target_link_libraries(server ${IBVERBS_LIB} ${RDMACM_LIB})

add_executable(client src/client.c)
target_link_libraries(client ${IBVERBS_LIB} ${RDMACM_LIB})
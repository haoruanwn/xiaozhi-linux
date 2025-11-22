// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2008-2023 100askTeam : Dongshan WEI <weidongshan@100ask.net> 
 * Discourse:  https://forums.100ask.net
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "ipc_udp.h"

// 定义UDP数据结构体
typedef struct upd_data_t {
    int socket_send;       // 发送数据的套接字
    int port_remote;         // 目标端口号
    int socket_recv;       // 接收数据的套接字
    int port_local;          // 源端口号
    struct sockaddr_in remote_addr;  // 目标地址结构体
}upd_data_t, *p_upd_data_t;

// 线程处理函数声明
static void* handle_udp_connection(void* arg);

// 发送数据的函数声明
static int udp_send_data(ipc_endpoint_t *pendpoint, const char *data, int len);

// 接收数据的函数声明
static int udp_recv_data(ipc_endpoint_t *pendpoint, unsigned char *data, int maxlen, int *retlen);

// 创建一个UDP类型的IPC端点
p_ipc_endpoint_t ipc_endpoint_create_udp(int port_local, int port_remote, transfer_callback_t cb, void *user_data)
{
    // 分配并清零UDP数据结构体
    p_upd_data_t pudpdata = (p_upd_data_t)calloc(1, sizeof(upd_data_t));
    // 分配并清零IPC端点结构体
    p_ipc_endpoint_t pendpoint = (p_ipc_endpoint_t)calloc(1, sizeof(ipc_endpoint_t));

    int fd_recv;
    struct sockaddr_in local_addr;
    struct sockaddr_in server_addr;

    if (!pudpdata || !pendpoint) {
        if (pendpoint)free(pendpoint);
        if (pudpdata)free(pudpdata);
        return NULL;            

    }

    // 关联UDP数据结构体和IPC端点结构体
    pendpoint->priv = pudpdata;
    pendpoint->cb = cb;
    pendpoint->user_data = user_data;
    pendpoint->send = udp_send_data;
    pendpoint->recv = udp_recv_data;

    // 设置远程和本地端口号
    pudpdata->port_remote = port_remote;
    pudpdata->port_local = port_local;

    // 1. 为了发送数据进行网络初始化
    // 创建UDP套接字
    int fd_send = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_send < 0) {
        perror("Failed to create UDP socket for audio client");
        if (pendpoint)free(pendpoint);
        if (pudpdata)free(pudpdata);
        return NULL;            
}

    // 初始化服务器地址结构
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_remote); // 使用传入的端口号
    if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        if (pendpoint)free(pendpoint);
        if (pudpdata)free(pudpdata);
        return NULL;            
}

    // 保存套接字和服务器地址信息到UDP数据结构体
    pudpdata->socket_send = fd_send;
    pudpdata->remote_addr = server_addr;    
    
    // 2. 为了接收数据进行网络初始化
    // 创建UDP套接字
    if ((fd_recv = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Failed to create socket");
        if (pendpoint)free(pendpoint);
        if (pudpdata)free(pudpdata);
        return NULL;            
}

    pudpdata->socket_recv = fd_recv;

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port_local);

    if (inet_pton(AF_INET, "127.0.0.1", &local_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        if (pendpoint)free(pendpoint);
        if (pudpdata)free(pudpdata);
        return NULL;            
}

    // 绑定套接字
    if (bind(fd_recv, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("Failed to bind socket");
        close(fd_recv);
        if (pendpoint)free(pendpoint);
        if (pudpdata)free(pudpdata);
        return NULL;            
    }

    // 如果有回调函数，创建线程处理UDP连接
    if (cb) {
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_udp_connection, pendpoint) != 0) {
            perror("Failed to create thread");

            if (pendpoint)free(pendpoint);
            if (pudpdata)free(pudpdata);
            return NULL;            
        }
    }

    return pendpoint;    
}

// 销毁IPC端点，释放相关资源
void ipc_endpoint_destroy_udp(p_ipc_endpoint_t pendpoint)
{
    free(pendpoint->priv);
    free(pendpoint);
}

/**
 * 处理UDP连接的线程函数
 */
static void* handle_udp_connection(void* arg)
{
    ipc_endpoint_t *pendpoint = (ipc_endpoint_t*)arg;
    p_upd_data_t pudpdata = (p_upd_data_t)pendpoint->priv;

    int fd_recv = pudpdata->socket_recv;
    char buffer[2048];

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    ssize_t bytes_received;

    printf("Listening on port_local %d\n", pudpdata->port_local);

    while (1) {
        // 接收数据
        bytes_received = recvfrom(fd_recv, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);
        if (bytes_received > 0) {
            // 处理接收到的数据
            if (pendpoint->cb) {
                pendpoint->cb(buffer, bytes_received, pendpoint->user_data);
            }
        }
    }

    pthread_exit(NULL);
}

/**
 * 发送数据到指定endpoint的通用函数
 */
static int udp_send_data(ipc_endpoint_t *pendpoint, const char *data, int len)
{
    p_upd_data_t pudpdata = (p_upd_data_t)pendpoint->priv;

    // 获取套接字文件描述符
    int fd = pudpdata->socket_send;
    // 获取服务器地址的指针
    struct sockaddr_in *p_server_addr = &pudpdata->remote_addr;

    // 检查套接字是否已初始化
    if (fd < 0) {
        fprintf(stderr, "UDP socket for audio server is not initialized\n");
        return -1;
    }

    // 发送数据到客户端
    ssize_t bytes_sent = sendto(fd, data, len, 0, (struct sockaddr *)p_server_addr, sizeof(*p_server_addr));
    // 检查发送的数据量是否与预期相符
    if (bytes_sent != len) {
        perror("Failed to send data to client");
        return -1;
    }

    // 发送成功
    return 0;
}

/**
 * 接收数据函数
 */
static int udp_recv_data(ipc_endpoint_t *pendpoint, unsigned char *data, int maxlen, int *retlen)
{
    p_upd_data_t pudpdata = (p_upd_data_t)pendpoint->priv;

    // 获取套接字文件描述符
    int fd = pudpdata->socket_recv;

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    ssize_t bytes_received;

    if (fd < 0) {
        fprintf(stderr, "UDP socket for audio client is not initialized\n");
        return -1;
    }

    // 接收数据
    bytes_received = recvfrom(fd, data, maxlen, 0, (struct sockaddr *)&client_addr, &client_len);
    if (bytes_received < 0) {
        perror("Failed to receive data from server");
        return -1;
    }

    *retlen = (int)bytes_received;

    return 0;
}

#pragma once

#include <unistd.h>

#include <mutex>

#include "common/protocol.h"

// 连接对象
struct Connection {
    explicit Connection(int socket_fd) : fd(socket_fd) {}  // 构造函数

    // 析构函数，对象销毁自动关闭socket
    ~Connection() {
        if (fd != -1) {
            close(fd);
        }
    }

    Connection(const Connection&) = delete;             // 禁止拷贝构造
    Connection& operator=(const Connection&) = delete;  // 禁止拷贝赋值

    MessageStatus send(const Message& message) {
        std::lock_guard<std::mutex> lock(send_mutex);  // 对象的发送锁
        return send_message(fd, message);
    }

    int fd;
    std::mutex send_mutex;
};
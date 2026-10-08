#include "common/net.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <iostream>

#include "common/protocol.h"

// 发送信息函数
bool send_all(int fd, const char* data, std::size_t length) {
    std::size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t sent = send(fd, data + total_sent, length - total_sent, MSG_NOSIGNAL);
        if (sent == -1) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("发送失败");
            return false;
        } else if (sent == 0) {
            std::cerr << "发送无进展\n";
            return false;
        }
        total_sent += static_cast<std::size_t>(sent);
        // std::cout << "累计发送: " << total_sent << "/" << length << " Byte(s)\n";
    }
    return true;
}

// 指定长度接收函数
bool recv_exact(int fd, char* data, std::size_t length) {
    std::size_t total_received = 0;
    while (total_received < length) {
        ssize_t received = recv(fd, data + total_received, length - total_received, 0);
        if (received == -1) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("接收失败");
            return false;
        } else if (received == 0) {
            std::cerr << "连接关闭\n";
            return false;
        }
        total_received += static_cast<std::size_t>(received);
        // std::cout << "累计接收: " << total_received << "/" << length << " Byte(s)\n";
    }
    return true;
}

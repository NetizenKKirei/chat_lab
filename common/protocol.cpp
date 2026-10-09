#include "common/protocol.h"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <nlohmann/json_fwd.hpp>
#include <string>

#include "common/net.h"

// 发送一条完整协议帧
MessageStatus send_message(int fd, const Message& message) {
    try {
        // 检查并编码消息body
        if (!message.payload.is_object()) {
            std::cerr << "消息负载必须是 JSON 对象\n";
            return MessageStatus::Rejected;
        }
        std::string body = message.payload.dump();
        if (body.size() > MAX_PAYLOAD_SIZE) {
            std::cerr << "消息长度超过上限(4MiB)\n";
            return MessageStatus::Rejected;
        }

        // 组装头部
        char header[8]{};
        header[0] = 'C';
        header[1] = 'H';
        header[2] = 1;
        header[3] = static_cast<char>(message.type);
        std::uint32_t body_length = htonl(static_cast<std::uint32_t>(body.size()));
        std::memcpy(header + 4, &body_length, sizeof(body_length));

        // 发送帧
        if (!send_all(fd, header, sizeof(header))) {
            return MessageStatus::Close;
        }
        if (!send_all(fd, body.data(), body.size())) {
            return MessageStatus::Close;
        };
        return MessageStatus::Success;
    } catch (const nlohmann::json::exception& error) {
        std::cerr << "消息JSON编码失败: " << error.what() << '\n';
        return MessageStatus::Rejected;
    }
}

// 接收一条完整协议帧
MessageStatus recv_message(int fd, Message& message) {
    // 检查header
    char header[8]{};
    if (!recv_exact(fd, header, sizeof(header))) {
        return MessageStatus::Close;
    }
    if (header[0] != 'C' || header[1] != 'H') {
        std::cerr << "魔数不正确!\n";
        return MessageStatus::Close;
    }
    if (header[2] != 1) {
        std::cerr << "不支持的协议版本\n";
        return MessageStatus::Close;
    }

    // 类型检查
    auto type = static_cast<MessageType>(static_cast<unsigned char>(header[3]));
    switch (type) {
        case MessageType::Login:
        case MessageType::List:
        case MessageType::Broadcast:
        case MessageType::Private:
        case MessageType::Response:
        case MessageType::Error:
            break;
        default:
            std::cerr << "未知消息类型\n";
            return MessageStatus::Close;
    }

    // 负载长度检查
    std::uint32_t body_length = 0;
    std::memcpy(&body_length, header + 4, sizeof(body_length));
    std::uint32_t length = ntohl(body_length);
    if (length == 0 || length > MAX_PAYLOAD_SIZE) {
        std::cerr << "负载长度不合法\n";
        return MessageStatus::
            Close;  // 程序内客户端发送前会检查负载长度，这里接收函数检查，检测到绕过检查的非标准客户端直接断开连接
    }

    // 读取body
    std::string body(length, '\0');
    if (!recv_exact(fd, body.data(), length)) {
        return MessageStatus::Close;
    }

    try {
        auto payload = nlohmann::json::parse(body);

        if (!payload.is_object()) {
            std::cerr << "消息负载必须是 JSON 对象\n";
            return MessageStatus::Rejected;
        }
        message.type = type;
        message.payload = payload;
        return MessageStatus::Success;
    } catch (const nlohmann::json::exception& error) {
        std::cerr << "消息JSON解析失败：" << error.what() << '\n';
        return MessageStatus::Rejected;
    }
}
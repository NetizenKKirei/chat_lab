#pragma once

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>

// 消息类型
enum class MessageType : std::uint8_t {
    Login = 1,
    List = 2,
    Broadcast = 3,
    Private = 4,
    Response = 5,
    Error = 6
};

inline constexpr std::size_t MAX_PAYLOAD_SIZE = std::size_t{4} * 1024 * 1024;  // 负载上限4MiB
inline constexpr std::size_t MAX_TEXT_SIZE = std::size_t{1024} * 1024;         // 正文上限1MiB

// 消息对象
struct Message {
    MessageType type;
    nlohmann::json payload;
};

// 协议收发结果
enum class MessageStatus { Success, Rejected, Close };

MessageStatus send_message(int fd, const Message& message);
MessageStatus recv_message(int fd, Message& message);
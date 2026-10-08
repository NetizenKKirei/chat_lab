#pragma once

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>

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

struct Message {
    MessageType type;
    nlohmann::json payload;
};

bool send_message(int fd, const Message& message);
bool recv_message(int fd, Message& message);
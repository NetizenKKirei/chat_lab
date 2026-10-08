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

inline constexpr std::size_t MAX_PAYLOAD_SIZE = std::size_t{4} * 1024 * 1024;

struct Message {
    MessageType type;
    nlohmann::json payload;
};

bool send_message(int fd, const Message& message);
bool recv_message(int fd, Message& message);
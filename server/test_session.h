#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "common/protocol.h"

// 定义测试状态
struct ServerTestState {
    bool active = false;
    unsigned int run_id = 0;

    // 测试约定
    std::uint64_t expected_messages = 0;
    std::size_t expected_data_bytes = 0;

    // 统计数据
    std::uint64_t received_messages = 0;
    std::uint64_t received_bytes = 0;
    std::uint64_t next_expected_sequence = 0;
    std::uint64_t data_errors = 0;

    // 时间戳
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point ended_at;

    // 上一次实时输出时的时间和累计计数
    std::chrono::steady_clock::time_point last_report_at;
    std::uint64_t last_report_messages = 0;
    std::uint64_t last_report_bytes = 0;
};

// 状态属于具体session；由该session线程顺序调用。
void print_test_stats(const ServerTestState& state);
void report_test_progress(ServerTestState& state);
Message handle_test(Message& request, ServerTestState& state);
Message handle_test_control(const Message& request, ServerTestState& state);

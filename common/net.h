#pragma once
#include <cstddef>

bool send_all(int fd, const char* data, std::size_t length);
bool recv_exact(int fd, char* data, std::size_t length);
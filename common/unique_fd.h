#pragma once

#include <unistd.h>

class UniqueFd {
   public:
    explicit UniqueFd(int fd) : fd_(fd) {}

    ~UniqueFd() {
        if (fd_ != -1) {
            close(fd_);
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    int get() const { return fd_; }

   private:
    int fd_;
};
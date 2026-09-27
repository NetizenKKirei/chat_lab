#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <ostream>

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::perror("socket 创建失败");
        return 1;
    }
    std::cout << "创建成功！文件描述符为：" << server_fd << std::endl;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9000);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int result = bind(server_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));

    if (result == -1) {
        std::perror("bind 失败");
        close(server_fd);
        return 1;
    }

    std::cout << "绑定成功: 127.0.0.1:9000" << std::endl;
    close(server_fd);
    return 0;
}
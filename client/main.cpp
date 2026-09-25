#include <iostream>
#include <string>

int main() {
    while (true) {
        std::string line;
        std::cout << "请输入文本：";
        if (!std::getline(std::cin, line)) {
            std::cout << "输入结束或读取失败！\n";
            break;
        }
        if (line.empty()) {
            std::cout << "输入不能为空！\n";
            continue;
        } else if (line == "/quit") {
            std::cout << "对话结束，再见！\n";
            break;
        }
        std::cout << "你发送了：" << line << std::endl;
    }
    return 0;
}
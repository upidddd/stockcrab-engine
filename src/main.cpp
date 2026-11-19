// StockCrab: the native program. Reads UCI commands from stdin.

#include <iostream>
#include <string>

#include "uci.h"

int main(int argc, char **argv) {
    std::ios::sync_with_stdio(false);
    uci::init(nullptr, true);
    if (argc > 1 && std::string(argv[1]) == "bench") {
        uci::run_bench();
        return 0;
    }
    std::cout << uci::name() << " by upidddd" << std::endl;
    std::string line;
    while (std::getline(std::cin, line))
        if (!uci::command(line)) break;
    uci::shutdown();
    return 0;
}

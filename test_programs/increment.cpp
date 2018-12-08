
/**
 * Demo program to demonstrate recursive process piping
 * All this does is increment the number provided.
 */

#include <iostream>

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::cout << std::stoi(line) + 1 << std::endl;
    }

    return 0;
}

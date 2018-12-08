/**
 * Demo program to demonstrate recursive process piping
 * All this does is output the number if it's non-prime, 
 * otherwise close program if it's prime.
 * That should trigger the process hierarchy to collapse, 
 * and the result can be harvested.
 */

#include <iostream>
#include <cmath>

bool is_prime(int input) {
    if (input <= 1) return false;

    // basic primality test, just check if any of the 
    // numbers up to sqrt(n) divide n

    int int_sqrt = (int) sqrt((double) input);
    for (int dividor = 2; dividor <= int_sqrt; ++dividor) {
        if (input % dividor == 0) return false;
    }

    // reached here?
    return true;
}

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!is_prime(std::stoi(line))) {
            std::cout << line << std::endl;
        } else {
            break;
        }
    }

    return 0;
}

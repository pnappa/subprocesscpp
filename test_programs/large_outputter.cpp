/**
 * Demo program to output a large number of characters for a series of switches
 * @author Patrick Nappa
 *
 * The reason for needing this program is to ensure that the pipes are flushed
 * timely within the process. The unix pipes only support ~65kB of data within
 * so we output several times that amount just to be sure.
 * This data must be split up via lines, as output is buffered via lines,
 * and it's not really expected that a line of input is larger than 65kB
 * We believe this is a limitation that we can't avoid.
 * -> https://unix.stackexchange.com/questions/11946/how-big-is-the-pipe-buffer
 */

#include <iostream>
#include <cassert>

// 1024 length string
constexpr ssize_t line_length = 1024;
const std::string basic_line = std::string(line_length-1, 'A');

void output_line(ssize_t& amount) {
    // duh, nothing to output!
    if (amount <= 0) return;

    if (amount < line_length) {
        std::cout << std::string(amount - 1, 'A') << std::endl;
    } else {
        std::cout << basic_line << std::endl;
    }

    amount -= line_length;
}

void prefixed_churn(const ssize_t amount) {
    assert(amount > 0 && "amount must be non-zero and positive");
    std::string line;

    ssize_t c_amount = amount;

    do {
        while (c_amount > 0) {
            output_line(c_amount);
        }

        c_amount = amount;
    } while (std::getline(std::cin, line));
}

void postfix_churn(const ssize_t amount) {
    assert(amount > 0 && "amount must be non-zero and positive");
    std::string line;
    ssize_t c_amount = amount;

    while (std::getline(std::cin, line)) {
        while (c_amount > 0) {
            output_line(c_amount);
        }
        c_amount = amount;
    }
}

void infinite_churn(const ssize_t amount) {
    assert(amount > 0 && "amount must be non-zero and positive");
    ssize_t c_amount = amount;
    while (true) {
        while (c_amount > 0) {
            output_line(c_amount);
        }

        c_amount = amount;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " + std::string(argv[0]) + " TYPE [amount]" << std::endl;
        std::cerr << "Where TYPE is either PRE, FOREACH, INFINITE" << std::endl;
        std::cerr << "PRE means lines with be output before each line of stdin is read" << std::endl;
        std::cerr << "FOREACH means output will be emitted after each line of stdin is processed" << std::endl;
        std::cerr << "INFINITE means an infinite stream of data will be emitted" << std::endl;
        std::cerr << "By default amount is 2^17 bytes, i.e. 131072 characters. This is split up into 1024 character lines (1023 plus newline)." << std::endl;
        std::cerr << "Cheers cunt" << std::endl;

        return EXIT_FAILURE;
    }

    // default output length
    ssize_t num_bytes = 1 << 17;
    if (argc >= 3) {
        num_bytes = std::stoi(std::string(argv[2]));
        if (num_bytes < 0) {
            std::cerr << "Amount of bytes emitted must be positive" << std::endl;
            return EXIT_FAILURE;
        }
    }

    std::string execution_type(argv[1]);
    if (execution_type == "PRE") {
        prefixed_churn(num_bytes);
    } else if (execution_type == "FOREACH") {
        postfix_churn(num_bytes);
    } else if (execution_type == "INFINITE") {
        infinite_churn(num_bytes); 
    } else {
        std::cerr << "please provide a valid type of execution" << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

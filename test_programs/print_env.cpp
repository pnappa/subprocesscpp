/**
 * Print environment variable values for each arg
 * usage: ./print_env SHELL
 * -> /usr/bin/bash
 */
 
// to allow us to use getenv
#include <stdlib.h>
#include <iostream>

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        char* env_val = getenv(argv[i]);
        std::cout << (env_val == nullptr ? "" : env_val)  << std::endl;
    }
}

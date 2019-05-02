/**
 * Echo all arguments verbatim
 */
#include <iostream>

int main(int argc, char* argv[]) {
    // print all arguments except the last one separated by a space
    for (int i = 1; i < argc-1; ++i) {
        std::cout << argv[i] << ' ';
    }
    // if there is a last one, print it without a trailing space
    if (argc > 1) std::cout << argv[argc-1];
    std::cout << std::endl;
}

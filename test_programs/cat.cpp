/**
 * Print out each of the files provided as arguments
 * - is not stdin
 * If none are provided, print stdin
 */

#include <vector>
#include <iostream>
#include <fstream>

int main(int argc, char* argv[]) {

    std::vector<std::ifstream> inFiles;
    for (int i = 1; i < argc; ++i) {
        inFiles.push_back(std::ifstream(argv[i]));
    }
    // do stdin
    if (inFiles.size() == 0) {
        int counter = 0;
        for (std::string line; std::getline(std::cin, line);) {
            ++counter;
            std::cout << line << std::endl;
        }
    } else {
        for (std::ifstream& f : inFiles) {
            for (std::string line; std::getline(f, line);) {
                std::cout << line << std::endl;
            }
        } 
    }
}


#include "subprocess.hpp"

#include <fstream>

int main() {
    subprocess::Process p("/bin/echo", {"lol"});
    std::ofstream outfile("cool.out");

    p.pipe_to(outfile);

    p.start();
}

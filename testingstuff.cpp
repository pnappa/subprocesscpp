#include "subprocess.hpp"

#include <fstream>

int main() {
    subprocess::Process p("/bin/echo", {"asjdlksaj"});
    p.output_to_file("cool.out");
    p.start();
}

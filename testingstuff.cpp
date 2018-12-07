#include "subprocess.hpp"

#include <fstream>

int main() {
    subprocess::Process p("/bin/echo", {"asjdlksaj"});
    p.output_to_file("cool.out");
    subprocess::Process p2("/bin/grep", {"asj"});
    p2.output_to_file("nekcool.out");
    p.pipe_to(p2);
    p.start();
}




// v1 - pipe_to(Process&& proc);
//p.pipe_to(Process("/bin/grep", {"-i", "cool"}));
//// equivalent to 
//p.pipe_to({"/bin/grep", {"-i", "cool"}});
//
//// v2 - pipe_to(Process& proc);
//Process p3("/bin/grep", {"-i", "cool"});
//p.pipe_to(p3);


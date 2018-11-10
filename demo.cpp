#include <list>
#include <string>
#include <iostream>

#include "subprocess.hpp"

void echoString(std::string in) {
    std::cout << "output: " << in;
}

int main() {
    // execute bc and pass it some equations
    std::list<std::string> inputs = {"1+1\n", "2^333\n", "32-32\n"};
    subprocess::execute("/usr/bin/bc", 
                              {}, 
                              inputs, 
                              echoString);

    // grep over some inputs
    inputs = {"12232\n", "hello, world\n", "Hello, world\n", "line: Hello, world!\n"};
    subprocess::execute("/bin/grep", {"-i", "Hello, world"}, inputs, echoString);

    // execute a process and extract all lines outputted
    inputs.clear(); // provide no stdin
    int status;
    std::vector<std::string> vec = subprocess::checkOutput("/usr/bin/time", {"sleep", "1"}, inputs, status);
    for (const std::string& s : vec) {
        std::cout << "output: " << s << '\t';
        std::cout << "line length:" << s.length() << std::endl;
    }
    std::cout << "process finished with an exit code of: " << status << std::endl;

    // execute sleep asynchronously, and block when needing the output
    std::future<int> futureStatus = subprocess::async("/bin/sleep", {"3"}, inputs, [](std::string) {});
    // if this wasn't async, this line wouldn't print until after the process finished!
    std::cout << "executing sleep..." << std::endl;
    std::cout << "sleep executed with exit status: " << futureStatus.get() << std::endl;

    // simulate pipes between programs: lets launch cat to provide input into a grep process!
    // Note! This will read all output into a single vector, then provide this as input into the second process
    //      If you want a function that isn't as memory intensive, consider streamOutput, which provides an iterator interface
    inputs = {"12232\n", "hello, world\n", "Hello, world\n", "line: Hello, world!\n"};
    vec = subprocess::checkOutput("/bin/cat", {}, inputs, status);
    inputs = std::list<std::string>(vec.begin(), vec.end());
    subprocess::execute("/bin/grep", {"-i", "^Hello, world$"}, inputs, echoString);

    // stream output from a process
    inputs = {"12232\n", "hello, world\n", "Hello, world\n", "line: Hello, world!\n"};
    subprocess::ProcessStream ps("/bin/grep", {"-i", "^Hello, world$"}, inputs);
    for (std::string out : ps) {
        std::cout << "received: " << out;
    }
}

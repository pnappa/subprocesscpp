# SubprocessCPP

[![travis build](https://img.shields.io/travis/pnappa/subprocesscpp/master.svg)](https://img.shields.io/travis/pnappa/subprocesscpp/master.svg) ![codecov coverage](https://img.shields.io/codecov/c/github/pnappa/subprocesscpp.svg) ![nice](https://img.shields.io/badge/legit-this%20makes%20the%20project%20look%20better-brightgreen.svg)

A neat header-only library that allows you to execute processes either synchronously or asynchronously, whilst providing input and handling the generated input. No more calling `exec` in a C++ program!

This library uses some C++11 features, and tries to be as idiomatic to modern C++.

# Usage
Simply include the `subprocess.hpp` file.

An example program is provided in `demo.cpp`, but here's some snippets:

```C++
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
```

# Requirements
Not much:
 - A POSIX environment (Linux, Mac, \*BSD)
 - A modern >=C++11 compiler

Unfortunately, I am not as familiar with Windows to write code for it, if you want to provide some code that works for this platform, be my guest! But, don't forget to include appropriate header guards so that it works cross-platform.

# API
### TODO: ....detail what each of the functions _should_ be used for.

```C++
old API (current for now):
int execute(const std::string& commandPath, const std::vector<std::string>& commandArgs, std::list<std::string>& stringInput, std::function<void(std::string)> lambda)
std::vector<std::string> checkOutput(const std::string& commandPath, const std::vector<std::string>& commandArgs, std::list<std::string>& stringInput, int& status)
std::future<int> async(const std::string commandPath, const std::vector<std::string> commandArgs, std::list<std::string> stringInput, std::function<void(std::string)> lambda)

// ctor for ProcessStream class
class ProcessStream(const std::string& commandPath, const std::vector<std::string>& commandArgs, std::list<std::string>& stringInput)

```

# License
This is dual-licensed under a MIT and GPLv3 license - so FOSS lovers can use it, whilst people restricted in companies to not open-source their program is also able to use this library :)

I don't know too much about licenses, so if I missed anything, please let me know.

# Future Features
Some stuff that I haven't written yet, but I wanna:
 - [X] Output streaming. Provide an iterator to allow iteration over the output lines, such that we don't have to load all in memory at once.
 - [ ] Thread-safe async lambda interactions. Provide a method to launch a process in async, but still allow writing to the list of stdin without a race condition.
 - [ ] A ping-ponging interface. This should allow incrementally providing stdin, then invoking the functor if output is emitted. Note that will likely not be possible if there's not performed asynchronously, or without using select. Using select is a bit annoying, because how do we differentiate between a command taking a while and it providing no input?
 - [ ] Provide a way to set environment variables (i can pretty easily do it via using `execvpe`, but what should the API look like?) 

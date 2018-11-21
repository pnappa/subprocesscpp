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
// Iterable can be stuff that can be iterated over (std::vector, etc.), all arguments other than commandPath are optional!
int execute(const std::string& commandPath, const Iterable& commandArgs, const Iterable& stdinInput, std::function<void(std::string)> lambda, const Iterable& environmentVariables);
// accepts iterators, the argument iterators are mandatory, the rest are optional
int execute(const std::string& commandPath, Iterator argsBegin, Iterator argsEnd, Iterator stdinBegin, Iterator stdinEnd, std::function<void(std::string)> lambda, Iterator envStart, Iterator envEnd);

// Iterable can be stuff that can be iterated over (std::vector, etc.), all arguments other than commandPath are optional!
std::vector<std::string> check_output(const std::string& commandPath, const Iterable& commandArgs, const Iterable& stdinInput, const Iterable& environmentVariables);
// accepts iterators, the argument iterators are mandatory, the rest are optional
std::vector<std::string> check_output(const std::string& commandPath, Iterator argsBegin, Iterator argsEnd, Iterator stdinBegin, Iterator stdinEnd, Iterator envStart, Iterator envEnd);

// we currently don't have asynchronous, daemon spawning, or iterable interaction yet.  coming soon(TM)
```

# License
This is dual-licensed under a MIT and GPLv3 license - so FOSS lovers can use it, whilst people restricted in companies to not open-source their program is also able to use this library :)

I don't know too much about licenses, so if I missed anything, please let me know.

# Future Features
Some stuff that I haven't written yet, but I wanna (see [this issue for a more in depth explanation of each](https://github.com/pnappa/subprocesscpp/issues/3)):
 - Asynchronous execution
 - Daemon spawning helpers (execute process and disown it, only need to consider where stdout should go).
 - Interactive processes (manually feed stdin and retrieve stdout)

#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <list>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

// unix process stuff
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace subprocess {

/**
 * A TwoWayPipe that allows reading and writing between two processes
 * must call initialize before being passed between processes or used
 * */
class TwoWayPipe {
private:
    //[0] is the output end of each pipe and [1] is the input end of
    // each pipe
    int input_pipe_file_descriptor[2];
    int output_pipe_file_descriptor[2];
    std::string internalBuffer;
    bool inStreamGood = true;
    bool endSelected = false;
    bool initialized = false;
    size_t currentSearchPos = 0;
    /**
     * closes the ends that aren't used (do we need to do this?
     * */
    void closeUnusedEnds() {
        // we don't need the input end of the input pipe
        // or the output end of the output pipe
        close(input_pipe_file_descriptor[1]);
        close(output_pipe_file_descriptor[0]);
    }

public:
    TwoWayPipe() = default;

    /**
     * initializes the TwoWayPipe the pipe can not be used until
     * this is called
     * */
    void initialize() {
        if (initialized) {
            return;
        }
        int res = pipe(input_pipe_file_descriptor);
        res += pipe(output_pipe_file_descriptor);
        if (res < 0) {
            // error occurred, check errno and throw relevant exception
        } else {
            initialized = true;
        }
    }

    /**
     * sets this to be the child end of the TwoWayPipe
     * linking the input and output ends to stdin and stdout/stderr
     * This call does nothing if it is already set as the child end
     * */
    bool setAsChildEnd() {
        if (endSelected) return false;
        endSelected = true;
        int tmp[2] = {input_pipe_file_descriptor[0], input_pipe_file_descriptor[1]};

        input_pipe_file_descriptor[0] = output_pipe_file_descriptor[0];
        input_pipe_file_descriptor[1] = output_pipe_file_descriptor[1];
        output_pipe_file_descriptor[0] = tmp[0];
        output_pipe_file_descriptor[1] = tmp[1];

        dup2(input_pipe_file_descriptor[0], STDIN_FILENO);
        dup2(output_pipe_file_descriptor[1], STDOUT_FILENO);
        dup2(output_pipe_file_descriptor[1], STDERR_FILENO);

        closeUnusedEnds();
        return true;
    }

    /**
     * sets this pipe to be the parent end of the TwoWayPipe
     * */
    bool setAsParentEnd() {
        if (endSelected) return false;
        endSelected = true;
        closeUnusedEnds();
        return true;
    }

    /**
     * writes a string to the pipe
     * @param input - the string to write
     * @return the number of bytes written
     * */
    size_t writeP(const std::string& input) {
        return write(output_pipe_file_descriptor[1], input.c_str(), input.size());
    }

    /**
     * @return true unless the last call to read either failed or
     * reached EOF
     * */
    bool isGood() const {
        return inStreamGood;
    }

    /**
     * reads up to n bytes into the internal buffer
     * @param n - the max number of bytes to read in
     * @return the number of bytes read in, -1 in the case of an
     * error
     * */
    ssize_t readToInternalBuffer() {
        char buf[256];
        ssize_t bytesCounted = -1;

        while ((bytesCounted = read(input_pipe_file_descriptor[0], buf, 256)) <= 0) {
            if (bytesCounted < 0) {
                if (errno != EINTR) { /* interrupted by sig handler return */
                    inStreamGood = false;
                    return -1;
                }
            } else if (bytesCounted == 0) { /* EOF */
                return 0;
            }
        }

        internalBuffer.append(buf, bytesCounted);
        return bytesCounted;
    }

    /**
     * Read line from the pipe - Not threadsafe
     * Blocks until either a newline is read
     *  or the other end of the pipe is closed
     * @return the string read from the pipe or the empty string if
     * there was not a line to read.
     * */
    std::string readLine() {
        size_t firstNewLine;

        while ((firstNewLine = internalBuffer.find_first_of('\n', currentSearchPos)) == std::string::npos) {
            currentSearchPos = internalBuffer.size();
            ssize_t bytesRead = readToInternalBuffer();
            if (bytesRead < 0) {
                std::cerr << "errno " << errno << " occurred" << std::endl;
                return "";
            }
            if (bytesRead == 0) {  // an EOF was reached, return the final line
                inStreamGood = false;
                return internalBuffer;
            }
        }
        // contains the characters after the
        // firstNewLine
        std::string endOfInternalBuffer = internalBuffer.substr(firstNewLine + 1);

        internalBuffer.erase(firstNewLine + 1);
        internalBuffer.swap(endOfInternalBuffer);
        currentSearchPos = 0;
        // now contains the first characters up to and
        // including the newline character
        return endOfInternalBuffer;
    }

    /**
     * tests pipe state, returns short which represents the status.
     * If POLLIN bit is set then it can be read from, if POLLHUP bit is
     * set then the write end has closed.
     * */
    short inPipeState(long wait_ms) {
        // file descriptor struct to check if pollin bit will be set
        struct pollfd fds = {.fd = input_pipe_file_descriptor[0], .events = POLLIN};
        // poll with no wait time
        int res = poll(&fds, 1, wait_ms);

        // if res < 0 then an error occurred with poll
        // POLLERR is set for some other errors
        // POLLNVAL is set if the pipe is closed
        if (res < 0 || fds.revents & (POLLERR | POLLNVAL)) {
            // TODO
            // an error occurred, check errno then throw exception if it is critical
        }
        // check if there is either data in the pipe or the other end is closed
        //(in which case a call will not block, it will simply return 0 bytes)
        return fds.revents;
    }

    bool canReadLine(long wait_ms) {
        if (!inStreamGood) {
            return false;
        }
        while (true) {
            size_t firstNewLine = internalBuffer.find_first_of('\n', currentSearchPos);
            if (firstNewLine != std::string::npos) {
                // this means that the next call to readLine won't
                // have to search through the whole string again
                currentSearchPos = firstNewLine;
                return true;
            }
            currentSearchPos = internalBuffer.size();
            short pipeState = inPipeState(wait_ms);
            if (!(pipeState & POLLIN)) {               // no bytes to read in pipe
                if (pipeState & POLLHUP) {             // the write end has closed
                    if (internalBuffer.size() == 0) {  // and theres no bytes in the buffer
                                                       // this pipe is done
                        inStreamGood = false;
                        return false;
                    }
                    // the buffer can be read as the final string
                    return true;
                }
                // pipe is still good, it just hasn't got anything in it
                return false;
            }

            ssize_t bytesRead = readToInternalBuffer();

            if (bytesRead < 0) {
                // error check errno and throw exception
                return false;  // for now just return false
            }
        }
    }

    void closeOutput() {
        close(output_pipe_file_descriptor[1]);
    }
};

/**
 * A Process class that wraps the creation of a seperate process
 * and gives acces to a TwoWayPipe to that process and its pid
 * The Process is not in a valid state until start is called
 * This class does not have ownership of the process, it merely maintains a
 * connection
 * */
class Process {
    pid_t pid;
    TwoWayPipe pipe;

public:
    Process() = default;

    /**
     * Starts a seperate process with the provided command and
     * arguments This also initializes the TwoWayPipe
     * @param commandPath - an absolute string to the program path
     * @param argsItBegin - the begin iterator to strings that
     * will be passed as arguments
     * @param argsItEnd - the end iterator to strings that
     * will be passed as arguments
     * @return TODO return errno returned by child call of execv
     * (need to use the TwoWayPipe)
     * */
    template <class InputIT>
    void start(const std::string& commandPath, InputIT argsItBegin, InputIT argsItEnd) {
        pid = 0;
        pipe.initialize();
        // construct the argument list (unfortunately,
        // the C api wasn't defined with C++ in mind, so
        // we have to abuse const_cast) see:
        // https://stackoverflow.com/a/190208
        std::vector<char*> cargs;
        // the process name must be first for execv
        cargs.push_back(const_cast<char*>(commandPath.c_str()));
        while (argsItBegin != argsItEnd) {
            cargs.push_back(const_cast<char*>((*argsItBegin).c_str()));
            argsItBegin++;
        }
        // must be terminated with a nullptr for execv
        cargs.push_back(nullptr);

        pid = fork();
        // child
        if (pid == 0) {
            pipe.setAsChildEnd();

            // ask kernel to deliver SIGTERM
            // in case the parent dies
            prctl(PR_SET_PDEATHSIG, SIGTERM);

            execv(commandPath.c_str(), cargs.data());
            // Nothing below this line
            // should be executed by child
            // process. If so, it means that
            // the execl function wasn't
            // successfull, so lets exit:
            exit(1);
        }
        pipe.setAsParentEnd();
    }

    template <typename Rep = long>
    bool isReady(std::chrono::duration<Rep> timeout = std::chrono::duration<long>(0)) {
        if (timeout.count() < 0) {
            return pipe.canReadLine(-1);
        }
        auto end = std::chrono::high_resolution_clock::now() + timeout;
        auto ms_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
        
        do {
            if (pipe.canReadLine(std::max(ms_remaining.count(), 0L))) {
                return true;
            }
            ms_remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end - std::chrono::high_resolution_clock::now());
        } while (ms_remaining.count() > 0);
        return false;
    }

    template <typename Rep = long>
    std::string readLine(std::chrono::duration<Rep> timeout = std::chrono::duration<long>(-1)) {
        if (isReady(timeout) && pipe.isGood()) {
            return pipe.readLine();
        }
        return "";
    }

    size_t write(const std::string& input) {
        return pipe.writeP(input);
    }

    void sendEOF() {
        pipe.closeOutput();
    }

    bool isGood() const {
        return pipe.isGood();
    }

    /**
     * blocks until the process exits and returns the exit
     * closeUnusedEnds
     * */
    int waitUntilFinished() {
        int status;
        waitpid(pid, &status, 0);
        return status;
    }
};

/**
 * Execute a subprocess and optionally call a function per line of stdout.
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param commandArgs   - the extra arguments for an executable e.g. {"argument 1", "henlo"}
 * @param stdinInput    - a list of inputs that will be piped into the processes' stdin
 * @param lambda        - a function that is called with every line from the executed process (default NOP function)
 * @param env           - a list of environment variables that the process will execute with (default nothing)
 */
int execute(const std::string& commandPath, const std::vector<std::string>& commandArgs,
        const std::vector<std::string>& stdinInput,
        const std::function<void(std::string)>& lambda = [](std::string) {},
        const std::vector<std::string>& env = {});

/**
 * Execute a subprocess and optionally call a function per line of stdout.
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param commandArgs   - the extra arguments for an executable e.g. {"argument 1", "henlo"}
 * @param stdinBegin    - an InputIterator to provide stdin
 * @param stdinEnd      - the end of the InputIterator range for stdin
 * @param lambda        - a function that is called with every line from the executed process (default NOP function)
 * @param env           - a list of environment variables that the process will execute with (default nothing)
 */
template <class InputIt>
int execute(const std::string& commandPath, const std::vector<std::string>& commandArgs, InputIt stdinBegin,
        InputIt stdinEnd, const std::function<void(std::string)>& lambda = [](std::string) {},
        const std::vector<std::string>& env = {});

/**
 * Execute a subprocess and retrieve the output of the command
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param commandArgs   - the extra arguments for an executable e.g. {"argument 1", "henlo"}
 * @param stdinInput    - a list of inputs that will be piped into the processes' stdin
 * @param env           - a list of environment variables that the process will execute with (default nothing)
 */
std::vector<std::string> check_output(const std::string& commandPath,
        const std::vector<std::string>& commandArgs, const std::vector<std::string>& stdioInput,
        const std::vector<std::string>& env = {});

/**
 * Execute a subprocess and retrieve the output of the command
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param commandArgs   - the extra arguments for an executable e.g. {"argument 1", "henlo"}
 * @param stdinBegin    - an InputIterator to provide stdin
 * @param stdinEnd      - the end of the InputIterator range for stdin
 * @param env           - a list of environment variables that the process will execute with (default nothing)
 */
template <class InputIt>
std::vector<std::string> check_output(const std::string& commandPath,
        const std::vector<std::string>& commandArgs, InputIt stdioBegin, InputIt stdioEnd,
        const std::vector<std::string>& env = {});

// // TODO: what if the process terminates? consider error handling potentials...
// class ProcessStream {
//     public:
//         ProcessStream(const std::string& commandPath, const std::vector<std::string>& commandArgs);

//         // write a line to the subprocess's stdin
//         void write(const std::string& inputLine);
//         // read a line and block until received (or until timeout reached)
//         template<typename Rep>
//         std::string read(std::chrono::duration<Rep> timeout=-1);
//         // if there is a line for reading
//         template<typename Rep>
//         bool ready(std::chrono::duration<Rep> timeout=0);

//         ProcessStream& operator<<(const std::string& inputLine);
//         ProcessStream& operator>>(std::string& outputLine);
// };

/**
 * Execute a process, inputting stdin and calling the functor with the stdout
 * lines.
 * @param commandPath - an absolute string to the program path
 * @param commandArgs - a vector of arguments that will be passed to the process
 * @param stringInput - a feed of strings that feed into the process (you'll typically want to end them with a
 * newline)
 * @param lambda - the function to execute with every line output by the process
 * @return the exit status of the process
 * */
int execute(const std::string& commandPath, const std::vector<std::string>& commandArgs,
        std::list<std::string>& stringInput /* what pumps into stdin */,
        std::function<void(std::string)> lambda) {
    Process childProcess;
    childProcess.start(commandPath, commandArgs.begin(), commandArgs.end());

    // while our string queue is working,
    while (!stringInput.empty()) {
        // write our input to the process's stdin pipe
        std::string newInput = stringInput.front();
        stringInput.pop_front();
        childProcess.write(newInput);
    }

    childProcess.sendEOF();

    // iterate over each line output by the child's stdout, and call
    // the functor
    std::string input;
    while ((input = childProcess.readLine()).size() > 0) {
        lambda(input);
    }

    return childProcess.waitUntilFinished();
}

/* convenience fn to return a list of outputted strings */
std::vector<std::string> checkOutput(const std::string& commandPath,
        const std::vector<std::string>& commandArgs,
        std::list<std::string>& stringInput /* what pumps into stdin */, int& status) {
    std::vector<std::string> retVec;
    status = execute(
            commandPath, commandArgs, stringInput, [&](std::string s) { retVec.push_back(std::move(s)); });
    return retVec;
}

/* spawn the process in the background asynchronously, and return a future of the status code */
std::future<int> async(const std::string commandPath, const std::vector<std::string> commandArgs,
        std::list<std::string> stringInput, std::function<void(std::string)> lambda) {
    // spawn the function async - we must pass the args by value into the async lambda
    // otherwise they may destruct before the execute fn executes!
    // whew, that was an annoying bug to find...
    return std::async(std::launch::async,
            [&](const std::string cp, const std::vector<std::string> ca, std::list<std::string> si,
                    std::function<void(std::string)> l) { return execute(cp, ca, si, l); },
            commandPath, commandArgs, stringInput, lambda);
}

/* TODO: refactor up this function so that there isn't duplicated code - most of this is identical to the
 * execute fn execute a program and stream the output after each line input this function calls select to
 * check if outputs needs to be pumped after each line input. This means that if the line takes too long to
 * output, it may be not input into the functor until another line is fed in. You may modify the delay to try
 * and wait longer until moving on. This delay must exist, as several programs may not output a line for each
 * line input. Consider grep - it will not output a line if no match is made for that input. */
class ProcessStream {
    Process childProcess;

public:
    ProcessStream(const std::string& commandPath, const std::vector<std::string>& commandArgs,
            std::list<std::string>& stringInput) {
        childProcess.start(commandPath, commandArgs.begin(), commandArgs.end());

        // while our string queue is working,
        while (!stringInput.empty()) {
            // write our input to the
            // process's stdin pipe
            std::string newInput = stringInput.front();
            stringInput.pop_front();
            childProcess.write(newInput);
        }
        // now we finished chucking in the string, send
        // an EOF
        childProcess.sendEOF();
    }

    ~ProcessStream() {
        childProcess.waitUntilFinished();
    }

    struct iterator {
        ProcessStream* ps;
        bool isFinished = false;
        // current read line of the process
        std::string cline;

        iterator(ProcessStream* ps) : ps(ps) {
            // increment this ptr, because nothing exists initially
            ++(*this);
        }
        // ctor for end()
        iterator(ProcessStream* ps, bool) : ps(ps), isFinished(true) {}

        const std::string& operator*() const {
            return cline;
        }

        /* preincrement */
        iterator& operator++() {
            // iterate over each line output by the child's stdout, and call the functor
            cline = ps->childProcess.readLine();
            if (cline.empty()) {
                isFinished = true;
            }
            return *this;
        }

        /* post increment */
        iterator operator++(int) {
            iterator old(*this);
            ++(*this);
            return old;
        }

        bool operator==(const iterator& other) const {
            return other.ps == this->ps && this->isFinished == other.isFinished;
        }

        bool operator!=(const iterator& other) const {
            return !((*this) == other);
        }
    };

    iterator begin() {
        return iterator(this);
    }

    iterator end() {
        return iterator(this, true);
    }
};

}  // end namespace subprocess

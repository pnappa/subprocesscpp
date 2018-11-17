#pragma once

#include <algorithm>
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
        pipe(input_pipe_file_descriptor);
        pipe(output_pipe_file_descriptor);
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
        int cnt;
        ssize_t bytesCounted = -1;

        while ((bytesCounted = read(input_pipe_file_descriptor[0], buf, 256)) <= 0) {
            if (bytesCounted < 0) {
                if (errno != EINTR) { /* interrupted by sig handler return */
                    inStreamGood = false;
                    return -1;
                }
            } else if (bytesCounted == 0) { /* EOF */
                inStreamGood = false;
                return 0;
            }
        }

        internalBuffer.append(buf, bytesCounted);
        return bytesCounted;
    }

    /**
     * read line from the pipe - Not threadsafe
     * blocks until either a newline is read or the other end of the
     * pipe is closed
     * @return the string read from the pipe or the empty string if
     * there was not a line to read.
     * */
    std::string readLine() {
        size_t firstNewLine;
        size_t currentSearchPos = 0;
        while ((firstNewLine = internalBuffer.find_first_of('\n', currentSearchPos)) == std::string::npos) {
            size_t currentSearchPos = internalBuffer.size();
            ssize_t bytesRead = readToInternalBuffer();
            if (bytesRead < 0) {
                std::cerr << "errno " << errno << " occurred" << std::endl;
                return "";
            }
            if (bytesRead == 0) {
                return internalBuffer;
            }
        }
        // contains the characters after the
        // firstNewLine
        std::string endOfInternalBuffer = internalBuffer.substr(firstNewLine + 1);

        internalBuffer.erase(firstNewLine + 1);
        internalBuffer.swap(endOfInternalBuffer);

        // now contains the first characters up to and
        // including the newline character
        return endOfInternalBuffer;
    }

    bool closeOutput() {
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
     * @param commandArgs - an iterable container of strings that
     * will be passed as arguments
     * @return TODO return errno returned by child call of execv
     * (need to use the TwoWayPipe)
     * */
    template <class Iterable>
    void start(const std::string& commandPath, Iterable& args) {
        pid = 0;
        pipe.initialize();
        // construct the argument list (unfortunately,
        // the C api wasn't defined with C++ in mind, so
        // we have to abuse const_cast) see:
        // https://stackoverflow.com/a/190208
        std::vector<char*> cargs;
        // the process name must be first for execv
        cargs.push_back(const_cast<char*>(commandPath.c_str()));
        for (const std::string& arg : args) {
            cargs.push_back(const_cast<char*>(arg.c_str()));
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

    std::string readLine() {
        return pipe.readLine();
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
 * Execute a process, inputting stdin and calling the functor with the stdout
 * lines.
 * @param commandPath - an absolute string to the program path
 * @param commandArgs - a vector of arguments that will be passed to the process
 * @param stringInput - a feed of strings that feed into the process (you'll
 * typically want to end them with a newline)
 * @param lambda - the function to execute with every line output by the process
 * @return the exit status of the process
 * */
int execute(const std::string& commandPath, const std::vector<std::string>& commandArgs,
        std::list<std::string>& stringInput /* what pumps into stdin */,
        std::function<void(std::string)> lambda) {
    Process childProcess;
    childProcess.start(commandPath, commandArgs);

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
    std::string input = childProcess.readLine();
    while (childProcess.isGood()) {
        lambda(input);
        input = childProcess.readLine();
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

/* spawn the process in the background asynchronously, and return a future of
 * the status code */
std::future<int> async(const std::string commandPath, const std::vector<std::string> commandArgs,
        std::list<std::string> stringInput, std::function<void(std::string)> lambda) {
    // spawn the function async - we must pass the args by value
    // into the async lambda otherwise they may destruct before the
    // execute fn executes! whew, that was an annoying bug to
    // find...
    return std::async(std::launch::async,
            [&](const std::string cp, const std::vector<std::string> ca, std::list<std::string> si,
                    std::function<void(std::string)> l) { return execute(cp, ca, si, l); },
            commandPath, commandArgs, stringInput, lambda);
}

/* TODO: refactor up this function so that there isn't duplicated code - most of
 * this is identical to the execute fn execute a program and stream the output
 * after each line input this function calls select to check if outputs needs to
 * be pumped after each line input. This means that if the line takes too long
 * to output, it may be not input into the functor until another line is fed in.
 * You may modify the delay to try and wait longer until moving on.
 * This delay must exist, as several programs may not output a line for each
 * line input.
 *  Consider grep - it will not output a line if no match is made for that
 * input. */
class ProcessStream {
    Process childProcess;

public:
    ProcessStream(const std::string& commandPath, const std::vector<std::string>& commandArgs,
            std::list<std::string>& stringInput) {
        childProcess.start(commandPath, commandArgs);

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

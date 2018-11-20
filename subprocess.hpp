#pragma once

#include <algorithm>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <list>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <type_traits>
#include <iterator>
#include <vector>
#include <utility>

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

    const std::string commandPath;
    std::vector<char*> processArgs;
    std::vector<char*> envVariables;

    // construct the argument list (unfortunately, the C api wasn't defined with C++ in mind, so
    // we have to abuse const_cast) see: https://stackoverflow.com/a/190208
    // this returns a null terminated vector that contains a list of non-const char ptrs
    template<class Iter>
    static std::vector<char*> toNullTerminatedCharIterable(Iter begin, Iter end) {
        // TODO: insert test to check if our iterable store strings..?
        // well it'll fail in the push_back stage anyway

        std::vector<char*> charArrayPlex;

        // the process name must be first for execv
        //charArrayPlex.push_back(const_cast<char*>(input.c_str()));
        for (auto it = begin; it != end; ++it) {
            charArrayPlex.push_back(const_cast<char*>((*it).c_str()));
        }
        // must be terminated with a nullptr for execv
        charArrayPlex.push_back(nullptr);

        return charArrayPlex;
    }

public:
    template <class ArgIt, class EnvIt>
    Process(const std::string& commandPath, ArgIt argBegin, ArgIt argEnd, EnvIt envBegin, EnvIt envEnd) : commandPath(commandPath) {
        pid = 0;
        pipe.initialize();
            
        // generate a vector that is able to be passed into exec for the process arguments
        processArgs = toNullTerminatedCharIterable(argBegin, argEnd);
        // process args must start with the processes name
        processArgs.insert(processArgs.begin(), const_cast<char*>(commandPath.c_str()));

        // ditto for the env variables
        envVariables = toNullTerminatedCharIterable(envBegin, envEnd);
        envVariables.insert(envVariables.begin(), const_cast<char*>(commandPath.c_str()));
    }

    /**
     * Starts a seperate process with the provided command and
     * arguments This also initializes the TwoWayPipe
     * @param commandPath - an absolute string to the program path
     * @param commandArgs - an iterable container of strings that
     * will be passed as arguments
     * @return TODO return errno returned by child call of execv
     * (need to use the TwoWayPipe)
     * */
    void start() {
        this->pid = fork();
        // child
        if (pid == 0) {
            pipe.setAsChildEnd();

            // ask kernel to deliver SIGTERM
            // in case the parent dies
            prctl(PR_SET_PDEATHSIG, SIGTERM);

            execvpe(commandPath.c_str(), processArgs.data(), envVariables.data());
            // Nothing below this line should be executed by child process. If so, it means that
            // the execl function wasn't successfull, so lets exit:
            exit(1);
        }
        pipe.setAsParentEnd();
    }

    std::string readLine() {
        if (!pipe.isGood()) return "";
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

/* begin https://stackoverflow.com/a/16974087 */
// a way to provide optional iterators to a function that do nothing
struct DevNull {
    template<typename T> T& operator=(T const&) { }
    template<typename T> operator T&() { static T dummy; return dummy; }
};

struct DevNullIterator {
    DevNull operator*() const { return DevNull();}
    DevNullIterator& operator++() { return *this; }
    DevNullIterator operator++(int) const { return *this; }
    DevNullIterator* operator->() { return this; }
    // always equivalent (a for loop should instantly terminate!)
    bool operator==(DevNullIterator&) const { return true; }
    bool operator!=(DevNullIterator&) const { return false; }
};
/* end https://stackoverflow.com/a/16974087 */

/* hm, I copied this from somewhere, dunno where */
template <typename T>
struct is_iterator {
    static char test(...);

    template <typename U,
             typename=typename std::iterator_traits<U>::difference_type,
             typename=typename std::iterator_traits<U>::pointer,
             typename=typename std::iterator_traits<U>::reference,
             typename=typename std::iterator_traits<U>::value_type,
             typename=typename std::iterator_traits<U>::iterator_category
                 > static long test(U&&);

    constexpr static bool value = std::is_same<decltype(test(std::declval<T>())),long>::value;
};
/* begin https://stackoverflow.com/a/29634934 */
namespace detail
{
    // To allow ADL with custom begin/end
    using std::begin;
    using std::end;
 
    template <typename T>
    auto is_iterable_impl(int)
    -> decltype (
        begin(std::declval<T&>()) != end(std::declval<T&>()), // begin/end and operator !=
        ++std::declval<decltype(begin(std::declval<T&>()))&>(), // operator ++
        *begin(std::declval<T&>()), // operator*
        std::true_type{});
 
    template <typename T>
    std::false_type is_iterable_impl(...);
 
}
 
template <typename T>
using is_iterable = decltype(detail::is_iterable_impl<T>(0));
/* end https://stackoverflow.com/a/29634934 */

static std::list<std::string> dummyVec = {};

/**
 * Execute a subprocess and optionally call a function per line of stdout.
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param firstArg      - the begin iterator for a list of arguments
 * @param lastArg       - the end iterator for a list of arguments
 * @param stdinBegin    - an InputIterator to provide stdin
 * @param stdinEnd      - the end of the InputIterator range for stdin
 * @param lambda        - a function that is called with every line from the executed process (default NOP function)
 * @param envBegin      - the begin of an iterator containing process environment variables to set
 * @param envEnd        - the end of the env iterator
 */
template<class ArgIt, class StdinIt = std::list<std::string>::iterator, 
         class EnvIt = std::list<std::string>::iterator, 
         typename = typename std::enable_if<is_iterator<ArgIt>::value, void>::type,
         typename = typename std::enable_if<is_iterator<StdinIt>::value, void>::type,
         typename = typename std::enable_if<is_iterator<EnvIt>::value, void>::type>
int execute(const std::string& commandPath, ArgIt firstArg, ArgIt lastArg, 
            StdinIt stdinBegin = dummyVec.begin(), StdinIt stdinEnd = dummyVec.end(), 
            const std::function<void(std::string)>& lambda = [](std::string){}, 
            EnvIt envBegin = dummyVec.begin(), EnvIt envEnd = dummyVec.end()) {

    Process childProcess(commandPath, firstArg, lastArg, envBegin, envEnd);
    childProcess.start();

    // write our input to the processes stdin pipe
    for (auto it = stdinBegin; it != stdinEnd; ++it) {
        childProcess.write(*it);
    }
    // close the stdin for the process
    childProcess.sendEOF();

    // iterate over each line output by the child's stdout, and call the functor
    std::string processOutput;
    while ((processOutput = childProcess.readLine()).size() > 0) {
        lambda(processOutput);
    }

    return childProcess.waitUntilFinished();
}

/**
 * Execute a subprocess and optionally call a function per line of stdout.
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param commandArgs   - the extra arguments for an executable e.g. {"argument 1", "henlo"} (default no arguments)
 * @param stdinInput    - a list of inputs that will be piped into the processes' stdin (default no stdin)
 * @param lambda        - a function that is called with every line from the executed process (default NOP function)
 * @param env           - a list of environment variables that the process will execute with (default nothing)
 */
template<class ArgIterable = std::vector<std::string>, 
         class StdinIterable = std::vector<std::string>, 
         class EnvIterable = std::vector<std::string>,
         typename = typename std::enable_if<is_iterable<ArgIterable>::value, void>::type,
         typename = typename std::enable_if<is_iterable<StdinIterable>::value, void>::type,
         typename = typename std::enable_if<is_iterable<EnvIterable>::value, void>::type>
int execute(const std::string& commandPath, const ArgIterable& commandArgs = {}, 
            const StdinIterable& stdinInput = {}, const std::function<void(std::string)>& lambda = [](std::string){}, 
            const EnvIterable& env = {}) {
    return execute(commandPath, commandArgs.begin(), commandArgs.end(), stdinInput.begin(), stdinInput.end(), lambda, env.begin(), env.end());
}

/**
 * Execute a subprocess and retrieve the output of the command
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param firstArg      - the begin iterator for a list of arguments
 * @param lastArg       - the end iterator for a list of arguments
 * @param stdinBegin    - an InputIterator to provide stdin
 * @param stdinEnd      - the end of the InputIterator range for stdin
 * @param lambda        - a function that is called with every line from the executed process (default NOP function)
 * @param envBegin      - the begin of an iterator containing process environment variables to set
 * @param envEnd        - the end of the env iterator
 */
template<class ArgIt, class StdinIt, class EnvIt, 
        typename = typename std::enable_if<is_iterator<ArgIt>::value, void>::type,
        typename = typename std::enable_if<is_iterator<StdinIt>::value, void>::type,
        typename = typename std::enable_if<is_iterator<EnvIt>::value, void>::type>
std::vector<std::string> check_output(const std::string& commandPath, ArgIt firstArg, ArgIt lastArg,
                                      StdinIt stdinBegin = DevNullIterator(), StdinIt stdinEnd = DevNullIterator(), 
                                      EnvIt envBegin = DevNullIterator(), EnvIt envEnd = DevNullIterator()) {
    std::vector<std::string> retVec;
    //int status = execute(commandPath, firstArg, lastArg, stdinBegin, stdinEnd, [&](std::string s) { retVec.push_back(std::move(s)); }, envBegin, envEnd);
    execute(commandPath, firstArg, lastArg, stdinBegin, stdinEnd, [&](std::string s) { retVec.push_back(std::move(s)); }, envBegin, envEnd);
    return retVec;
}

/**
 * Execute a subprocess and retrieve the output of the command
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param commandArgs   - the extra arguments for an executable e.g. {"argument 1", "henlo"}
 * @param stdinInput    - a list of inputs that will be piped into the processes' stdin
 * @param env           - a list of environment variables that the process will execute with (default nothing)
 */
template<class ArgIterable = std::vector<std::string>, 
         class StdinIterable = std::vector<std::string>, 
         class EnvIterable = std::vector<std::string>,
         typename = typename std::enable_if<is_iterable<ArgIterable>::value, void>::type,
         typename = typename std::enable_if<is_iterable<StdinIterable>::value, void>::type,
         typename = typename std::enable_if<is_iterable<EnvIterable>::value, void>::type>
std::vector<std::string> check_output(const std::string& commandPath, const ArgIterable& commandArgs = {}, 
                                      const StdinIterable& stdinInput = {}, const EnvIterable& env = {}) {
    return check_output(commandPath, commandArgs.begin(), commandArgs.end(), stdinInput.begin(), stdinInput.end(), env.begin(), env.end());
}

//// TODO: what if the process terminates? consider error handling potentials...
//class ProcessStream {
//    public:
//        ProcessStream(const std::string& commandPath, const std::vector<std::string>& commandArgs);
//
//        // write a line to the subprocess's stdin
//        void write(const std::string& inputLine);
//        // read a line and block until received (or until timeout reached)
//        template<typename Rep>
//        std::string read(std::chrono::duration<Rep> timeout=-1);
//        // if there is a line for reading
//        template<typename Rep>
//        bool ready(std::chrono::duration<Rep> timeout=0);
//
//        ProcessStream& operator<<(const std::string& inputLine);
//        ProcessStream& operator>>(std::string& outputLine);
//};

/* spawn the process in the background asynchronously, and return a future of the status code */
//std::future<int> async(const std::string commandPath, const std::vector<std::string> commandArgs,
//        std::list<std::string> stringInput, std::function<void(std::string)> lambda) {
//    // spawn the function async - we must pass the args by value into the async lambda
//    // otherwise they may destruct before the execute fn executes!
//    // whew, that was an annoying bug to find...
//    return std::async(std::launch::async,
//            [&](const std::string cp, const std::vector<std::string> ca, std::list<std::string> si,
//                    std::function<void(std::string)> l) { return execute(cp, ca, si, l); },
//            commandPath, commandArgs, stringInput, lambda);
//}

/* TODO: refactor up this function so that there isn't duplicated code - most of this is identical to the
 * execute fn execute a program and stream the output after each line input this function calls select to
 * check if outputs needs to be pumped after each line input. This means that if the line takes too long to
 * output, it may be not input into the functor until another line is fed in. You may modify the delay to try
 * and wait longer until moving on. This delay must exist, as several programs may not output a line for each
 * line input. Consider grep - it will not output a line if no match is made for that input. */
//class ProcessStream {
//    Process childProcess;
//
//public:
//    ProcessStream(const std::string& commandPath, const std::vector<std::string>& commandArgs,
//            std::list<std::string>& stringInput) {
//        childProcess.start(commandPath, commandArgs);
//
//        // while our string queue is working,
//        while (!stringInput.empty()) {
//            // write our input to the
//            // process's stdin pipe
//            std::string newInput = stringInput.front();
//            stringInput.pop_front();
//            childProcess.write(newInput);
//        }
//        // now we finished chucking in the string, send
//        // an EOF
//        childProcess.sendEOF();
//    }
//
//    ~ProcessStream() {
//        childProcess.waitUntilFinished();
//    }
//
//    struct iterator {
//        ProcessStream* ps;
//        bool isFinished = false;
//        // current read line of the process
//        std::string cline;
//
//        iterator(ProcessStream* ps) : ps(ps) {
//            // increment this ptr, because nothing exists initially
//            ++(*this);
//        }
//        // ctor for end()
//        iterator(ProcessStream* ps, bool) : ps(ps), isFinished(true) {}
//
//        const std::string& operator*() const {
//            return cline;
//        }
//
//        /* preincrement */
//        iterator& operator++() {
//            // iterate over each line output by the child's stdout, and call the functor
//            cline = ps->childProcess.readLine();
//            if (cline.empty()) {
//                isFinished = true;
//            }
//            return *this;
//        }
//
//        /* post increment */
//        iterator operator++(int) {
//            iterator old(*this);
//            ++(*this);
//            return old;
//        }
//
//        bool operator==(const iterator& other) const {
//            return other.ps == this->ps && this->isFinished == other.isFinished;
//        }
//
//        bool operator!=(const iterator& other) const {
//            return !((*this) == other);
//        }
//    };
//
//    iterator begin() {
//        return iterator(this);
//    }
//
//    iterator end() {
//        return iterator(this, true);
//    }
//};

}  // end namespace subprocess

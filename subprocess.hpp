#pragma once

#include <algorithm>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <iterator>
#include <list>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <cassert>
#include <deque>
#include <fstream>

// unix process stuff
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>


namespace subprocess {
namespace internal {
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
        bool failed = pipe(input_pipe_file_descriptor) < 0;
        failed |= pipe(output_pipe_file_descriptor) < 0;
        if (failed)
        {
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

    const std::string commandPath;
    std::vector<char*> processArgs;
    std::vector<char*> envVariables;

    // construct the argument list (unfortunately, the C api wasn't defined with C++ in mind, so
    // we have to abuse const_cast) see: https://stackoverflow.com/a/190208
    // this returns a null terminated vector that contains a list of non-const char ptrs
    template <class Iter>
    static std::vector<char*> toNullTerminatedCharIterable(Iter begin, Iter end) {
        // TODO: insert test to check if our iterable store strings..?
        // well it'll fail in the push_back stage anyway

        std::vector<char*> charArrayPlex;

        // the process name must be first for execv
        // charArrayPlex.push_back(const_cast<char*>(input.c_str()));
        for (auto it = begin; it != end; ++it) {
            charArrayPlex.push_back(const_cast<char*>((*it).c_str()));
        }
        // must be terminated with a nullptr for execv
        charArrayPlex.push_back(nullptr);

        return charArrayPlex;
    }

public:
    template <class ArgIt, class EnvIt>
    Process(const std::string& commandPath, ArgIt argBegin, ArgIt argEnd, EnvIt envBegin, EnvIt envEnd)
            : commandPath(commandPath) {
        pid = 0;
        pipe.initialize();

        // generate a vector that is able to be passed into exec for the process arguments
        processArgs = toNullTerminatedCharIterable(argBegin, argEnd);
        // process args must start with the processes name
        processArgs.insert(processArgs.begin(), const_cast<char*>(commandPath.c_str()));

        // ditto for the env variables
        envVariables = toNullTerminatedCharIterable(envBegin, envEnd);
    }

    /**
     * Starts a seperate process with the provided command and arguments 
     * @return TODO return errno returned by child call of execv
     * (need to use the TwoWayPipe)
     * */
    void start() {
        pid = fork();
        // child
        if (pid == 0) {
            pipe.setAsChildEnd();

            // ask kernel to deliver SIGTERM in case the parent dies
            // so we don't get zombies
            prctl(PR_SET_PDEATHSIG, SIGTERM);

            execvpe(commandPath.c_str(), processArgs.data(), envVariables.data());
            // Nothing below this line should be executed by child process. If so, it means that
            // the exec function wasn't successful, so lets exit:
            exit(1);
        }
        pipe.setAsParentEnd();
    }

    template <typename Rep = long>
    bool isReady(std::chrono::duration<Rep> timeout = std::chrono::duration<long>(0)) {
        if (timeout.count() < 0) {
            return pipe.canReadLine(-1);
        }
        return pipe.canReadLine(std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());
    }

    template <typename Rep = long>
    std::string readLine(std::chrono::duration<Rep> timeout = std::chrono::duration<long>(-1)) {
        if (isReady(timeout)) {
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

/* hm, I copied this from somewhere, dunno where */
template <typename T>
struct is_iterator {
    static char test(...);

    template <typename U, typename = typename std::iterator_traits<U>::difference_type,
            typename = typename std::iterator_traits<U>::pointer,
            typename = typename std::iterator_traits<U>::reference,
            typename = typename std::iterator_traits<U>::value_type,
            typename = typename std::iterator_traits<U>::iterator_category>
    static long test(U&&);

    constexpr static bool value = std::is_same<decltype(test(std::declval<T>())), long>::value;
};
/* begin https://stackoverflow.com/a/29634934 */
namespace detail {
// To allow ADL with custom begin/end
using std::begin;
using std::end;

template <typename T>
auto is_iterable_impl(int)
        -> decltype(begin(std::declval<T&>()) != end(std::declval<T&>()),  // begin/end and operator !=
                ++std::declval<decltype(begin(std::declval<T&>()))&>(),    // operator ++
                *begin(std::declval<T&>()),                                // operator*
                std::true_type{});

template <typename T>
std::false_type is_iterable_impl(...);

}  // namespace detail

template <typename T>
using is_iterable = decltype(detail::is_iterable_impl<T>(0));
/* end https://stackoverflow.com/a/29634934 */

// smallest possible iterable for the default arg values for the API functions that accept iterators
using DummyContainer = std::list<std::string>;
static DummyContainer dummyVec = {};
} // namespace internal

/**
 * Execute a subprocess and optionally call a function per line of stdout.
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param firstArg      - the begin iterator for a list of arguments
 * @param lastArg       - the end iterator for a list of arguments
 * @param stdinBegin    - an InputIterator to provide stdin
 * @param stdinEnd      - the end of the InputIterator range for stdin
 * @param lambda        - a function that is called with every line from the executed process (default NOP
 * function)
 * @param envBegin      - the begin of an iterator containing process environment variables to set
 * @param envEnd        - the end of the env iterator
 */
template <class ArgIt, class StdinIt = internal::DummyContainer::iterator, class EnvIt = internal::DummyContainer::iterator,
        typename = typename std::enable_if<internal::is_iterator<ArgIt>::value, void>::type,
        typename = typename std::enable_if<internal::is_iterator<StdinIt>::value, void>::type,
        typename = typename std::enable_if<internal::is_iterator<EnvIt>::value, void>::type>
int execute(const std::string& commandPath, ArgIt firstArg, ArgIt lastArg,
        StdinIt stdinBegin = internal::dummyVec.begin(), StdinIt stdinEnd = internal::dummyVec.end(),
        const std::function<void(std::string)>& lambda = [](std::string) {},
        EnvIt envBegin = internal::dummyVec.begin(), EnvIt envEnd = internal::dummyVec.end()) {
    internal::Process childProcess(commandPath, firstArg, lastArg, envBegin, envEnd);
    childProcess.start();

    // TODO: fix this so we can't block the input/output pipe. it should only require
    // reading from the process so-as to unclog their pipe. Pipes only have finite space! (~65k)
    // but remember, we may need to read more than one line per for loop (if a process outputs a lot of lines
    // per line read in, perhaps..?)

    // write our input to the processes stdin pipe
    for (auto it = stdinBegin; it != stdinEnd; ++it) {
        childProcess.write(*it);

        // propagate output as we need to ensure the output pipe isn't clogged
        while (childProcess.isReady()) {
            lambda(childProcess.readLine());
        }
    }
    // close the stdin for the process
    childProcess.sendEOF();

    // iterate over each line of remaining output by the child's stdout, and call the functor
    std::string processOutput;
    while ((processOutput = childProcess.readLine()).size() > 0) {
        lambda(processOutput);
    }

    return childProcess.waitUntilFinished();
}

/**
 * Execute a subprocess and optionally call a function per line of stdout.
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param commandArgs   - the extra arguments for an executable e.g. {"argument 1", "henlo"} (default no
 * arguments)
 * @param stdinInput    - a list of inputs that will be piped into the processes' stdin (default no stdin)
 * @param lambda        - a function that is called with every line from the executed process (default NOP
 * function)
 * @param env           - a list of environment variables that the process will execute with (default nothing)
 */
template <class ArgIterable = std::list<std::string>, class StdinIterable = std::list<std::string>,
        class EnvIterable = std::list<std::string>,
        typename = typename std::enable_if<internal::is_iterable<ArgIterable>::value, void>::type,
        typename = typename std::enable_if<internal::is_iterable<StdinIterable>::value, void>::type,
        typename = typename std::enable_if<internal::is_iterable<EnvIterable>::value, void>::type>
int execute(const std::string& commandPath, const ArgIterable& commandArgs = {},
        const StdinIterable& stdinInput = {},
        const std::function<void(std::string)>& lambda = [](std::string) {}, const EnvIterable& env = {}) {
    return execute(commandPath, commandArgs.begin(), commandArgs.end(), stdinInput.begin(), stdinInput.end(),
            lambda, env.begin(), env.end());
}

/**
 * Execute a subprocess and retrieve the output of the command
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param firstArg      - the begin iterator for a list of arguments
 * @param lastArg       - the end iterator for a list of arguments
 * @param stdinBegin    - an InputIterator to provide stdin
 * @param stdinEnd      - the end of the InputIterator range for stdin
 * @param lambda        - a function that is called with every line from the executed process (default NOP
 * function)
 * @param envBegin      - the begin of an iterator containing process environment variables to set
 * @param envEnd        - the end of the env iterator
 */
template <class ArgIt, class StdinIt = internal::DummyContainer::iterator, class EnvIt = internal::DummyContainer::iterator,
        typename = typename std::enable_if<internal::is_iterator<ArgIt>::value, void>::type,
        typename = typename std::enable_if<internal::is_iterator<StdinIt>::value, void>::type,
        typename = typename std::enable_if<internal::is_iterator<EnvIt>::value, void>::type>
std::vector<std::string> check_output(const std::string& commandPath, ArgIt firstArg, ArgIt lastArg,
        StdinIt stdinBegin = internal::dummyVec.begin(), StdinIt stdinEnd = internal::dummyVec.end(),
        EnvIt envBegin = internal::dummyVec.begin(), EnvIt envEnd = internal::dummyVec.end()) {
    std::vector<std::string> retVec;
    // int status = execute(commandPath, firstArg, lastArg, stdinBegin, stdinEnd, [&](std::string s) {
    // retVec.push_back(std::move(s)); }, envBegin, envEnd);
    execute(commandPath, firstArg, lastArg, stdinBegin, stdinEnd,
            [&](std::string s) { retVec.push_back(std::move(s)); }, envBegin, envEnd);
    return retVec;
}

/**
 * Execute a subprocess and retrieve the output of the command
 * @param commandPath   - the path of the executable to execute, e.g. "/bin/cat"
 * @param commandArgs   - the extra arguments for an executable e.g. {"argument 1", "henlo"}
 * @param stdinInput    - a list of inputs that will be piped into the processes' stdin
 * @param env           - a list of environment variables that the process will execute with (default nothing)
 */
template <class ArgIterable = std::vector<std::string>, class StdinIterable = std::vector<std::string>,
        class EnvIterable = std::vector<std::string>,
        typename = typename std::enable_if<internal::is_iterable<ArgIterable>::value, void>::type,
        typename = typename std::enable_if<internal::is_iterable<StdinIterable>::value, void>::type,
        typename = typename std::enable_if<internal::is_iterable<EnvIterable>::value, void>::type>
std::vector<std::string> check_output(const std::string& commandPath, const ArgIterable& commandArgs = {},
        const StdinIterable& stdinInput = {}, const EnvIterable& env = {}) {
    return check_output(commandPath, commandArgs.begin(), commandArgs.end(), stdinInput.begin(),
            stdinInput.end(), env.begin(), env.end());
}

// TODO: what if the process terminates? consider error handling potentials...
class Process {
        // need some list of processes this process is supposed to pipe to (if any)
        // XXX: what if the child processes are moved? should we keep a reference to the parent(s) then update us within their vector..?
        std::vector<Process*> successor_processes;
        std::vector<Process*> predecessor_processes;
        // TODO: how should we handle this..?
        // std::vector<std::ifstream> feedin_files;
        std::vector<std::ofstream> feedout_files;
        // the function to call every time a line is output
        // Functor func;
        bool started = false;
        bool finished = false;
        int retval;

        internal::Process owned_proc;

        std::deque<std::string> stdinput_queue;
    protected:

        void pump_input() {
            assert(started && "error: input propagated for inactive process");

            // write any queued stdinput
            while (!stdinput_queue.empty()) {
                this->write(stdinput_queue.front());
                stdinput_queue.pop_front();

                pump_output();
            }

            // each of the input files to this process has to be pumped too
            // for (std::ifstream* ifile : feedin_files) {
            //     // write as many lines from the input file until we run out
            //     for (std::string line; std::getline(*ifile, line); ) {
            //         // we gotta append a newline, getline omits it.
            //         this->write(line + "\n");
            //         pump_output();
            //     }
            // }
        }

        void pump_output() {
            assert(started && "error: input propagated for inactive process");

            while (owned_proc.isReady()) {
                std::string out = owned_proc.readLine();

                this->write_next(out);
            }
        }

        void write_next(const std::string& out) {

            // call functor
            // func(out);

            for (Process* succ_process : successor_processes) {
                succ_process->write(out);
            }
            // TODO: should I throw if cannot write to file..?
            for (std::ofstream& succ_file : feedout_files) {
                succ_file << out << std::flush;
            }
        }

    public:
        template<class ArgIterable = std::vector<std::string>, class Functor = std::function<void(std::string)>>
            Process(const std::string& commandPath, const ArgIterable& commandArgs, Functor func = [](std::string){}) : 
                owned_proc(commandPath, commandArgs.begin(), commandArgs.end(), internal::dummyVec.begin(), internal::dummyVec.end()) {
            }

        ~Process() {
            // err need to close all predecessors (if any)
            // if there is a cycle in the processes, this doesn't cause an infinite loop
            // as if they're already closed, they're a no-op.
            for (Process* pred_process : predecessor_processes) {
                pred_process->finish();
                
            }

            this->owned_proc.sendEOF();
            // process any remaining input/output
            finish();


            // TODO: is this right?
            for (Process* succ_process : successor_processes) {
                succ_process->finish();
            }

            // according to docs, this is not necessary, this'll happen in the dtor
            // for (std::ofstream& succ_file : feedout_files) {
            //     if (!succ_file) std::cout << "some error with file..?\n";
            //     succ_file.close();
            // }
        }

        // start the process and prevent any more pipes from being established.
        // may throw an exception?
        void start() {
            if (started) throw std::runtime_error("cannot start an already running process");
            owned_proc.start();
            started = true; 

            // recursively start all successor processes
            for (auto successor_proc : successor_processes) {
                successor_proc->start();
            }

            // push out any pending input
            pump_input();
            // propagate output some more
            pump_output();
        }

        int finish() {
            if (finished) return this->retval;
            pump_input();
            pump_output();
            pump_output();
            
            // iterate over each line of remaining output by the child's stdout, and call the functor
            std::string processOutput;
            while ((processOutput = owned_proc.readLine()).size() > 0) {
                this->write_next(processOutput);
            }

            this->retval = owned_proc.waitUntilFinished();
            finished = true;

            return this->retval;
        }

        bool is_started() { return started; }

        // write a line to the subprocess's stdin
        void write(const std::string& inputLine) {
            owned_proc.write(inputLine);
        }
        // read a line and block until received (or until timeout reached)
        template<typename Rep>
            std::string read(std::chrono::duration<Rep> timeout=-1);
        // if there is a line for reading (optionally 
        template<typename Rep>
            bool ready(std::chrono::duration<Rep> timeout=0);

        // pipe some data to the receiver process, and return the receiver process
        // we do this so we can have: process1.pipe_to(process2).pipe_to(process3)...etc
        // if pipes are set up there are some restrictions on using the << and >> operators.
        // if a process is receiving from another process, then they cannot use operator<< anymore
        //      hmm: what about if its done before .start()?
        // if a process is outputting to another, they cannot use operator>> 
        Process& pipe_to(Process& receiver) {
            successor_processes.push_back(&receiver);
            receiver.predecessor_processes.push_back(this);
            return receiver;
        }
        // ditto
        Process& operator>>(Process& receiver) { return this->pipe_to(receiver); }
        // XXX: removed this because the dtor wasn't handled well
        // for files
        // std::ofstream& pipe_to(std::ofstream& receiver) {
        //     feedout_files.push_back(&receiver);
        //     return receiver;
        // }
        void output_to_file(const std::string& filename) {
            feedout_files.push_back(std::ofstream(filename));
            if (!feedout_files.back().good()) throw std::runtime_error("error: file " + filename + " failed to open");
        }

        void output_to_file(std::ofstream&& file) {
            feedout_files.push_back(std::move(file));
            if (!feedout_files.back().good()) throw std::runtime_error("error: file is invalid");
        }

        // read a line into this process (so it acts as another line of stdin)
        // instead of string, probably should be typename Stringable, and use stringstream and stuff.
        //Process& operator<<(const std::string& inputLine);
        // retrieve a line of stdout from this process
        //Process& operator>>(std::string& outputLine);
        // write all stdout to file?
        // Process& operator>>(std::ofstream& outfile);

        // some other functions which maybe useful (perhaps take a timeout?)
        // returns whether it could terminate
        bool terminate();
        // a more...extreme way
        bool kill();
        // send arbitrary signals to the subprocess
        void signal(int signum);

        class iterator;

        // provide an iterator to iterate over the stdout produced
        iterator begin();
        iterator end();
};

}  // end namespace subprocess

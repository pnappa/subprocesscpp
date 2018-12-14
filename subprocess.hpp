#pragma once

#include <algorithm>
#include <chrono>
#include <set>
#include <stack>
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
#include <sstream>

// unix process stuff
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>


namespace subprocess {

    class Process;
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
        struct pollfd fds = {input_pipe_file_descriptor[0], POLLIN, 0};
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
    friend class subprocess::Process;

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
            charArrayPlex.push_back(strdup((*it).c_str()));
            //charArrayPlex.push_back(const_cast<char*>((*it).c_str()));
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
        processArgs.insert(processArgs.begin(), strdup(commandPath.c_str()));

        // ditto for the env variables
        envVariables = toNullTerminatedCharIterable(envBegin, envEnd);
    }

    ~Process() {
        // clean these up because they're created via strdup
        for (char* c : processArgs) free(c);
        for (char* c : envVariables) free(c);
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
    // XXX: what's a good way to return the return value, do we throw on non-zero return?
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
/**
 * A representation of a process. A process may be piped to one or more other processes or files.
 * Currently, this version does not support cyclic pipes, use AsyncProcess for that.
 */
class Process {
        // need some list of processes this process is supposed to pipe to (if any)
        // XXX: what if the child processes are moved? should we keep a reference to the parent(s) then update us within their vector..?
        std::vector<Process*> successor_processes;
        std::vector<Process*> predecessor_processes;
        static_assert(std::is_same<decltype(successor_processes), decltype(predecessor_processes)>::value, "processes must be stored in same container type");

        // TODO: how should we handle this..?
        //  The reason why its not trivial is that we may want to have a file pipe to multiple processes, and it feels
        //  like a waste to read from the file N times to output to N processes.
        // std::vector<std::ifstream> feedin_files;
        std::vector<std::ofstream> feedout_files;
        // the function to call every time a line is output
        // TODO: somehow make it use the ctor template type
        std::function<void(std::string)>* func = nullptr;

        bool started = false;
        bool finished = false;
        int retval;
        
        mutable size_t lines_written = 0;
        mutable size_t lines_received = 0;

        static size_t process_id_counter;

        internal::Process owned_proc;
        size_t identifier = process_id_counter++;

        std::deque<std::string> stdin_queue;
        std::deque<std::string> stdout_queue;

    protected:
        std::string get_identifier() {
            return std::to_string(identifier);
        }

        void pump_input() {
            assert(started && "error: input propagated for inactive process");

            // write any queued stdinput
            while (!stdin_queue.empty()) {
                this->write(stdin_queue.front());
                stdin_queue.pop_front();
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
            if (finished) return;
            assert(started && "error: output propagated for inactive process");

            while (owned_proc.isReady()) {
                std::string out = owned_proc.readLine();

                this->write_next(out);
            }
        }

        void write_next(const std::string& out) {
            assert(started && "error: input propagated for inactive process");

            // hold onto stdout if we don't have any successors or lambdas to execute
            if (successor_processes.empty() && feedout_files.empty() && func == nullptr) {
                stdout_queue.push_back(out);
            } else {
                // call functor
                (*func)(out);

                for (Process* succ_process : successor_processes) {
                    succ_process->write(out);
                }

                // TODO: should I throw if cannot write to file..?
                for (std::ofstream& succ_file : feedout_files) {
                    succ_file << out << std::flush;
                }
            }
        }

        /**
         * Read from this process (and all predecessors) until their and this process is finished.
         */
        void read_until_completion() {
            // XXX: should we colour this to check that it isn't a cyclic network, and throw an exception?
            if (finished) {
                return;
            }

            // we need to do block and read for the preds first (they must complete before this one can)
            for (Process* pred_process : predecessor_processes) {
                pred_process->read_until_completion();
            }

            // then block read for us & forward output
            std::string processOutput;
            while ((processOutput = owned_proc.readLine()).size() > 0) {
                this->write_next(processOutput);
                finished = true;
            }
        }

    public:
        template<class ArgIterable = decltype(internal::dummyVec), class Functor = std::function<void(std::string)>>
            Process(const std::string& commandPath, const ArgIterable& commandArgs = internal::dummyVec) : 
                owned_proc(commandPath, commandArgs.begin(), commandArgs.end(), internal::dummyVec.begin(), internal::dummyVec.end()) {
            }
        template<class ArgIterable = decltype(internal::dummyVec), class Functor = std::function<void(std::string)>>
            Process(const std::string& commandPath, const ArgIterable& commandArgs, Functor func) : 
                owned_proc(commandPath, commandArgs.begin(), commandArgs.end(), internal::dummyVec.begin(), internal::dummyVec.end()) {
                    // TODO: change back to new Functor(func), when I'm able to set the member variables type in the ctor.
                    this->func = new std::function<void(std::string)>(func);
            }

        /**
         * Get a graphvis compatible representation of the process network (DOT format)
         */
        std::string get_network_topology() {
            std::stringstream ret;

            ret << "digraph G {\n";

            std::set<Process*> visited_processes;
            std::stack<Process*> to_visit;

            to_visit.emplace(this);

            while (!to_visit.empty()) {
                Process* top = to_visit.top();
                to_visit.pop();
                // ignore the already visited
                if (visited_processes.count(top)) continue;

                visited_processes.emplace(top);

                // add the label for this process
                ret << top->get_identifier() << " [label=\"" << top->owned_proc.processArgs[0] << "\"];\n";

                // add edges for each of the parents and children, then queue them up to be visited
                // as predecessor_procs and successor_procs are symmetric, we only need to add one
                for (Process* proc : top->predecessor_processes) {
                    ret << proc->get_identifier() << "->" << top->get_identifier() << ";\n";
                    to_visit.emplace(proc); 
                }

                for (Process* proc : top->successor_processes) {
                    to_visit.emplace(proc); 
                }

            }

            ret << "}\n";

            return ret.str();
        }

        virtual ~Process() {
            // what needs to be done here is that output from predecessors needs to be forced until completion

            // need to close all predecessors (if any)
            for (Process* pred_process : predecessor_processes) {
                pred_process->finish();
            }

            // this->owned_proc.sendEOF();
            // process any remaining input/output
            finish();

            // do the same for outputting processes
            // XXX: i don't think I need/should do this, right?
            // for (Process* succ_process : successor_processes) {
            //     succ_process->finish();
            // }

            // our function is dynamically allocated (how else do we test for null functors..?)
            delete func;
        }

        // start the process and prevent any more pipes from being established.
        // may throw an exception?
        void start() {
            // ignore an already started process
            if (started) return;

            owned_proc.start();
            started = true; 

            // recursively start all predecessor processes
            // do this to ensure that if this process relies on a predecessor's input, then it will terminate.
            for (auto pred_process : predecessor_processes) {
                pred_process->start();
            }
            
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
            read_until_completion();
            pump_output();
            
            return owned_proc.waitUntilFinished();
        }

        bool is_started() const { return started; }

        // write a line to the subprocess's stdin
        void write(const std::string& inputLine) {
            if (finished) throw std::runtime_error("cannot write to a finished process");

            if (is_started()) {
                this->lines_written++;

                owned_proc.write(inputLine);

                pump_output();

            // if it hasn't been started, then we queue up the input for later
            } else {
                stdin_queue.push_front(inputLine);
            }
        }

        // read a line and block until received (or until timeout reached)
        template <typename Rep = long>
            std::string read(std::chrono::duration<Rep> timeout = std::chrono::duration<Rep>(-1)){
                std::string outputLine;

                if (!started || finished) {
                    throw std::runtime_error("cannot read line from inactive process");
                }

                if (successor_processes.size() > 0 || feedout_files.size() > 0 || func != nullptr) {
                    throw std::runtime_error("manually reading line from process that is piped from/has a functor is prohibited");
                }

                lines_written++;

                // we may have lines of output to "use" from earlier
                if (!stdout_queue.empty()) {
                    outputLine = stdout_queue.front();
                    stdout_queue.pop_front();
                } else {
                    outputLine = owned_proc.readLine(timeout);
                }

                return outputLine;
            }
        // if there is a line for reading (optionally 
        template <typename Rep = long>
            bool ready(std::chrono::duration<Rep> timeout=0) {
                return owned_proc.isReady(timeout);
            }

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
            // XXX: in some compilers this causes a warning, whilst in others omitting it causes an error.
            feedout_files.push_back(std::move(std::ofstream(filename)));
            if (!feedout_files.back().good()) throw std::runtime_error("error: file " + filename + " failed to open");
        }

        // can't seem to get this one working..?
        //void output_to_file(std::ofstream&& file) {
        //    feedout_files.push_back(std::move(file));
        //    if (!feedout_files.back().good()) throw std::runtime_error("error: file is invalid");
        //}

        // read a line into this process (so it acts as another line of stdin)
        // instead of string, probably should be typename Stringable, and use stringstream and stuff.
        Process& operator<<(const std::string& inputLine) {
            this->write(inputLine);
            return *this;
        }

        // retrieve a line of stdout from this process (blocking) into the string
        Process& operator>>(std::string& outputLine) {
            outputLine = read();
            return *this;
        }

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

// initialise the id counter, dumb c++ standard doesn't allow it
size_t Process::process_id_counter = 0;

/**
 * An async equivalent of Process
 * It constantly blocks for input to allow cyclic process flows
 */
class AsyncProcess : Process {

    std::future<int> retval;

    public:
        template<class ArgIterable = decltype(internal::dummyVec), class Functor = std::function<void(std::string)>>
        AsyncProcess(const std::string& commandPath, const ArgIterable& commandArgs = internal::dummyVec, Functor func = [](std::string){}) : 
            Process(commandPath, commandArgs, func) { }

        ~AsyncProcess() {

        }

        void start() {

        }
};

}  // end namespace subprocess

#pragma once

#include <string>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <functional>
#include <tuple>
#include <vector>
#include <list>
#include <future>

// unix process stuff
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <cstring>



namespace subprocess {

/* execute a process, inputting stdin and calling the functor with the stdout lines.
 * returns the status code of the executed program
 * */

int execute(const std::string& commandPath, 
        const std::vector<std::string>& commandArgs, 
        std::list<std::string>& stringInput /* what pumps into stdin */,
        std::function<void(std::string)> lambda) {
    // based off https://stackoverflow.com/a/6172578
    pid_t pid = 0;
    int inpipefd[2];
    int outpipefd[2];

    
    // construct the argument list (unfortunately, the C api wasn't defined with C++ in mind, so we have to abuse const_cast)
    // see: https://stackoverflow.com/a/190208
    std::vector<char*> cargs;
    // the process name must be first for execv
    cargs.push_back(const_cast<char*>(commandPath.c_str()));
    for (const std::string& arg : commandArgs) {
        cargs.push_back(const_cast<char*>(arg.c_str()));
    }
    // must be terminated with a nullptr for execv
    cargs.push_back(nullptr);

    pipe(inpipefd);
    pipe(outpipefd);
    pid = fork();
    // child
    if (pid == 0) {
        dup2(outpipefd[0], STDIN_FILENO);
        dup2(inpipefd[1], STDOUT_FILENO);
        dup2(inpipefd[1], STDERR_FILENO);

        // XXX: test (close the stdin..?)
        close(outpipefd[1]);

        //ask kernel to deliver SIGTERM in case the parent dies
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        
        execv(commandPath.c_str(), cargs.data());
        // Nothing below this line should be executed by child process. If so, 
        // it means that the execl function wasn't successfull, so lets exit:
        exit(1);
    }

    close(outpipefd[0]);
    close(inpipefd[1]);

    // while our string queue is working, 
    while (!stringInput.empty()) {
        // write our input to the process's stdin pipe
        std::string newInput = stringInput.front();
        stringInput.pop_front();
        write(outpipefd[1], newInput.c_str(), newInput.size());
    }
    // now we finished chucking in the string, send an EOF
    close(outpipefd[1]);

    // iterate over each line output by the child's stdout, and call the functor
    FILE* childStdout = fdopen(inpipefd[0], "r");
    char* line = nullptr;
    ssize_t nread;
    size_t len;
    while ((nread = getline(&line, &len, childStdout)) != -1) {
        lambda(std::string(line)); 

        // free up the memory allocated by getline
        free(line);
        line = nullptr;
    }
    if (line != nullptr) free(line);

    fclose(childStdout);
    int status;
    waitpid(pid, &status, 0);

    return status;
}

/* convenience fn to return a list of outputted strings */
std::vector<std::string> checkOutput(const std::string& commandPath, 
        const std::vector<std::string>& commandArgs, 
        std::list<std::string>& stringInput /* what pumps into stdin */,
        int& status) {
    std::vector<std::string> retVec;
    status = execute(commandPath, commandArgs, stringInput, [&](std::string s) { retVec.push_back(std::move(s)); });
    return retVec;
}

/* spawn the process in the background asynchronously, and return a future of the status code */
std::future<int> async(const std::string commandPath, const std::vector<std::string> commandArgs, std::list<std::string> stringInput, std::function<void(std::string)> lambda) {
    // spawn the function async - we must pass the args by value into the async lambda
    // otherwise they may destruct before the execute fn executes!
    // whew, that was an annoying bug to find...
    return std::async(std::launch::async, 
            [&](const std::string cp, 
                const std::vector<std::string> ca,
                std::list<std::string> si,
                std::function<void(std::string)> l) { return execute(cp, ca, si, l); }, commandPath, commandArgs, stringInput, lambda);
}

} // end namespace subprocess

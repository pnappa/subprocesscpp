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

#include "subprocess_utils.hpp"

namespace subprocess {

/** 
 * Execute a process, inputting stdin and calling the functor with the stdout lines.
 * @param commandPath - an absolute string to the program path
 * @param commandArgs - a vector of arguments that will be passed to the process
 * @param stringInput - a feed of strings that feed into the process (you'll typically want to end them with a newline)
 * @param lambda - the function to execute with every line output by the process
 * @return the exit status of the process
 * */
int execute(const std::string& commandPath, 
        const std::vector<std::string>& commandArgs, 
        std::list<std::string>& stringInput /* what pumps into stdin */,
        std::function<void(std::string)> lambda) {
    
    Process childProcess;
    childProcess.start(commandPath,commandArgs);
    
    // while our string queue is working, 
    while (!stringInput.empty()) {
        // write our input to the process's stdin pipe
        std::string newInput = stringInput.front();
        stringInput.pop_front();
        childProcess.pipe.writeP(newInput);
    }
    
    childProcess.pipe.closeOutput();

    // iterate over each line output by the child's stdout, and call the functor
    std::string input = childProcess.pipe.readLine();
    while(childProcess.pipe.isGood()){
        lambda(input);
        input = childProcess.pipe.readLine();
    }

    return childProcess.waitUntilFinished();
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


/* TODO: refactor up this function so that there isn't duplicated code - most of this is identical to the execute fn
 * execute a program and stream the output after each line input 
 * this function calls select to check if outputs needs to be pumped after each line input. 
 * This means that if the line takes too long to output, 
 *  it may be not input into the functor until another line is fed in.
 * You may modify the delay to try and wait longer until moving on.
 * This delay must exist, as several programs may not output a line for each line input.
 *  Consider grep - it will not output a line if no match is made for that input. */
class ProcessStream {
    Process childProcess;

public:
    ProcessStream(const std::string& commandPath,
        const std::vector<std::string>& commandArgs,
        std::list<std::string>& stringInput) {
            
        childProcess.start(commandPath,commandArgs);
        
        // while our string queue is working, 
        while (!stringInput.empty()) {
            // write our input to the process's stdin pipe
            std::string newInput = stringInput.front();
            stringInput.pop_front();
            childProcess.pipe.writeP(newInput);
        }
        // now we finished chucking in the string, send an EOF
        childProcess.pipe.closeOutput();
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
            cline = ps->childProcess.pipe.readLine();
            if(cline.empty())
            {
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

} // end namespace subprocess

#pragma once

#include <string>
#include <iostream>

// unix process stuff
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <cstring>


namespace subprocess {
    
    /**
     * A TwoWayPipe that allows reading and writing between two processes
     * must call initialize before being passed between processes or used
     * */
class TwoWayPipe {
    private:
        //[0] is the output end of each pipe and [1] is the input end of each pipe
        int input_pipe_file_descriptor[2];
        int output_pipe_file_descriptor[2];
        std::string internalBuffer;
        bool inStreamGood = true;
        bool endSelected = false;
    public:
        TwoWayPipe() = default;
        
        /**
         * initializes the TwoWayPipe the pipe can not be used until this is called
         * */
        void initialize(){
            pipe(input_pipe_file_descriptor);
            pipe(output_pipe_file_descriptor);
        }
    private:
        /**
         * closes the ends that aren't used (do we need to do this?
         * */
        void closeUnusedEnds() {
            //we don't need the input end of the input pipe or the output end of the output pipe
            close(input_pipe_file_descriptor[1]);
            close(output_pipe_file_descriptor[0]);
        }

    public:
    
        /**
         * sets this to be the child end of the TwoWayPipe
         * linking the input and output ends to stdin and stdout/stderr
         * This call does nothing if it is already set as the child end
         * */
        bool setAsChildEnd() {
            if(endSelected) return false;
            endSelected = true;
            int tmp[2] = {input_pipe_file_descriptor[0],input_pipe_file_descriptor[1]};

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
            if(endSelected) return false;
            endSelected = true;
            closeUnusedEnds();
            return true;
        }
        
        /**
         * writes a string to the pipe 
         * @param input - the string to write
         * @return the number of bytes written
         * */
        size_t writeP(const std::string &input) {
            return write(output_pipe_file_descriptor[1], input.c_str(), input.size());
        }

        /**
         * @return true unless the last call to read either failed or reached EOF
         * */
        bool isGood(){
            return inStreamGood;
        }

        /**
        * reads up to n bytes into the internal buffer
        * @param n - the max number of bytes to read in
        * @return the number of bytes read in, -1 in the case of an error
        * */
        ssize_t readToInternalBuffer()
        {
            char buf[256];
            int cnt;
            ssize_t bytesCounted = -1;

            while((bytesCounted = read(input_pipe_file_descriptor[0], buf, 256))<=0){
                if (bytesCounted < 0) {
                    if (errno != EINTR){ /* interrupted by sig handler return */
                        inStreamGood = false;
                        return -1;
                    }
                }
                else if (bytesCounted == 0)  /* EOF */
                {
                    inStreamGood = false;
                    return 0;
                }
            }
            
            internalBuffer.append(buf,bytesCounted);
            return bytesCounted;
        }

        /**
        * read line from the pipe - Not threadsafe
        * blocks until either a newline is read or the other end of the pipe is closed
        * @return the string read from the pipe or the empty string if there was not a line to read.
        * */
        std::string readLine() {
            size_t firstNewLine;
            size_t currentSearchPos = 0;
            while((firstNewLine = internalBuffer.find_first_of('\n',currentSearchPos))==std::string::npos)
            {
                size_t currentSearchPos = internalBuffer.size();
                ssize_t bytesRead = readToInternalBuffer();
                if(bytesRead<0){
                    std::cerr << "errno " << errno << " occurred" << std::endl;
                    return "";
                }
                if(bytesRead==0)
                {
                    return internalBuffer;
                }
            }
            //contains the characters after the firstNewLine
            std::string endOfInternalBuffer = internalBuffer.substr(firstNewLine+1);
            
            
            internalBuffer.erase(firstNewLine+1);
            internalBuffer.swap(endOfInternalBuffer);
            
            //now contains the first characters up to and including the newline character
            return endOfInternalBuffer;
        }
        
        bool closeOutput(){
            close(output_pipe_file_descriptor[1]);
        }
    };
    
    /**
     * A Process class that wraps the creation of a seperate process
     * and gives acces to a TwoWayPipe to that process and its pid
     * The Process is not in a valid state until start is called
     * This class does not have ownership of the process, it merely maintains a connection
     * */
    class Process{
        public:
        pid_t pid;
        TwoWayPipe pipe;
        
        Process() = default;
        
        /**
         * Starts a seperate process with the provided command and arguments
         * This also initializes the TwoWayPipe
         * @param commandPath - an absolute string to the program path
         * @param commandArgs - an iterable container of strings that will be passed as arguments
         * @return TODO return errno returned by child call of execv (need to use the TwoWayPipe)
         * */
        template <class Iterable>
        void start(const std::string& commandPath,Iterable& args){
            
            pid = 0;
            pipe.initialize();
            // construct the argument list (unfortunately, the C api wasn't defined with C++ in mind, so we have to abuse const_cast)
            // see: https://stackoverflow.com/a/190208
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
        
                //ask kernel to deliver SIGTERM in case the parent dies
                prctl(PR_SET_PDEATHSIG, SIGTERM);
                
                execv(commandPath.c_str(), cargs.data());
                // Nothing below this line should be executed by child process. If so, 
                // it means that the execl function wasn't successfull, so lets exit:
                exit(1);
            }
            pipe.setAsParentEnd();
        }
        
        /**
         * blocks until the process exits and returns the exit closeUnusedEnds
         * */
        int waitUntilFinished(){
            int status;
            waitpid(pid, &status, 0);
            return status;
        }
    };
}
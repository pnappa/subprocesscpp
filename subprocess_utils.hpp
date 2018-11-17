namespace subprocess {
class TwoWayPipe {
	private:
		//[0] is the output end of each pipe and [1] is the input end of each pipe
		int input_pipe_file_descriptor[2];
		int output_pipe_file_descriptor[2];
        std::string internalBuffer;
        bool inStreamGood = true;
        
	public:
		TwoWayPipe() {
			pipe(input_pipe_file_descriptor);
			pipe(output_pipe_file_descriptor);
		}
	private:
		void closeUnusedEnds() {
			//we don't need the input end of the input pipe or the output end of the output pipe
			close(input_pipe_file_descriptor[1]);
			close(output_pipe_file_descriptor[0]);
		}

	public:
		bool setAsChildEnd() {
		    int tmp[2] = {input_pipe_file_descriptor[0],input_pipe_file_descriptor[1]};

		    input_pipe_file_descriptor[0] = output_pipe_file_descriptor[0];
			input_pipe_file_descriptor[1] = output_pipe_file_descriptor[1];
			output_pipe_file_descriptor[0] = tmp[0];
			output_pipe_file_descriptor[1] = tmp[1];
		    
			dup2(input_pipe_file_descriptor[0], STDIN_FILENO);
			dup2(output_pipe_file_descriptor[1], STDOUT_FILENO);
			dup2(output_pipe_file_descriptor[1], STDERR_FILENO);

			closeUnusedEnds();
		}

		bool setAsParentEnd() {
			closeUnusedEnds();
		}

		size_t writeP(std::string &input) {
			return write(output_pipe_file_descriptor[1], input.c_str(), input.size());
		}

        
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
            size_t bytesCounted = -1;

            bytesCounted = read(input_pipe_file_descriptor[0], buf, 256);
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

            internalBuffer.append(buf,bytesCounted);
            return bytesCounted;
        }

		/**
		* read line from the pipe
		* blocks until either a newline is read or the other end of the pipe is closed
		* @return the string read from the pipe or the empty string if there was not a line to read.
		* */
		std::string readLine() {
		    size_t firstNewLine;
		    size_t currentSearchPos = 0;
		    while((firstNewLine = internalBuffer.find_first_of('\n',currentSearchPos))==std::string::npos)
		    {
		        size_t currentSearchPos = internalBuffer.size();
		        size_t bytesRead = readToInternalBuffer();
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
}
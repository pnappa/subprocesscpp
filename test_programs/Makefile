CXX=g++
CXXFLAGS=-g -std=c++11 -Wall -pedantic
LIBS=-lpthread

.PHONY: all clean

all: print_env tee_if_nonprime increment large_outputter echo

print_env: print_env.cpp
	$(CXX) $(CXXFLAGS) print_env.cpp -o print_env $(LIBS)

tee_if_nonprime: tee_if_nonprime.cpp
	$(CXX) $(CXXFLAGS) tee_if_nonprime.cpp -o tee_if_nonprime $(LIBS)

increment: increment.cpp
	$(CXX) $(CXXFLAGS) increment.cpp -o increment $(LIBS)

large_outputter: large_outputter.cpp
	$(CXX) $(CXXFLAGS) large_outputter.cpp -o large_outputter $(LIBS)

echo: echo.cpp
	$(CXX) $(CXXFLAGS) echo.cpp -o echo $(LIBS)

clean:
	rm -rvf print_env tee_if_nonprime increment large_outputter


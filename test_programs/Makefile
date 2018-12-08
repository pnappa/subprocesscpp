CXX=g++
CXXFLAGS=-g -std=c++11 -Wall -pedantic
LIBS=-lpthread

.PHONY: all clean

all: print_env tee_if_nonprime increment

print_env: print_env.cpp
	$(CXX) $(CXXFLAGS) print_env.cpp -o print_env $(LIBS)

tee_if_nonprime: tee_if_nonprime.cpp
	$(CXX) $(CXXFLAGS) tee_if_nonprime.cpp -o tee_if_nonprime $(LIBS)

increment: increment.cpp
	$(CXX) $(CXXFLAGS) increment.cpp -o increment $(LIBS)

clean:
	rm -rvf print_env tee_if_nonprime increment


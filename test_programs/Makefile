CXX=g++
CXXFLAGS=-g -std=c++11 -Wall -pedantic
LIBS=-lpthread

.PHONY: all clean

all: print_env

print_env: print_env.cpp
	$(CXX) $(CXXFLAGS) print_env.cpp -o print_env $(LIBS)

clean:
	rm -rvf print_env


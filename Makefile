
CXX=g++
CXXFLAGS=-g -std=c++11 -Wall -pedantic
LIBS=-lpthread

.PHONY: all clean
all: demo check

clean:
	rm -fv demo test coverage

demo: demo.cpp subprocess.hpp
	$(CXX) $(CXXFLAGS) demo.cpp -o demo $(LIBS)

check: test
	valgrind ./test

test: test.cpp subprocess.hpp
	$(CXX) $(CXXFLAGS) test.cpp -o test $(LIBS)

coverage: test.cpp subprocess.hpp
	$(CXX) $(CXXFLAGS) -fprofile-arcs -ftest-coverage test.cpp -o coverage $(LIBS)
	.codecov/run_coverage.sh


CXX=g++
CXXFLAGS=-g -std=c++11
# temporary for pnappa
LIBS=-lpthread

.PHONY: all clean
all: demo test

clean:
	rm -fv demo test coverage

demo: demo.cpp subprocess.hpp
	$(CXX) $(CXXFLAGS) demo.cpp -o demo $(LIBS)

test: test.cpp subprocess.hpp
	$(CXX) $(CXXFLAGS) test.cpp -o test $(LIBS)
	# run the testsuite (-s makes it nice and verbose)
	# XXX: should we make this verbose?
	./test -s

coverage: test.cpp subprocess.hpp
	$(CXX) $(CXXFLAGS) -fprofile-arcs -ftest-coverage test.cpp -o coverage $(LIBS)
	.codecov/run_coverage.sh

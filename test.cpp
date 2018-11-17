
/**
 * Testing for the subprocess library.
 * Uses the Catch2 testing library (https://github.com/catchorg/Catch2)
 */

#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include "catch.hpp"

#include "subprocess.hpp"

TEST_CASE("basic echo execution", "[subprocess::execute]") {
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    subprocess::execute("/bin/echo", {"hello"}, inputs, [&](std::string s) { outputs.push_back(s); });
    REQUIRE(outputs.size() == 1);
    // echo appends a newline by default
    REQUIRE(outputs.front() == "hello\n");
}

TEST_CASE("no trailing newline echo execution", "[subprocess::execute]") {
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    subprocess::execute("/bin/echo", {"-n", "hello"}, inputs, [&](std::string s) { outputs.push_back(s); });

    REQUIRE(outputs.size() == 1);
    REQUIRE(outputs.front() == "hello");
}

TEST_CASE("non existent executable", "[subprocess::execute]") {
    // TODO: try and run a non-existent executable, what should happen..?

    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/wangwang", {}, inputs,
            [](std::string) { FAIL("this functor should never have been called"); });

    // process should have failed..?
    REQUIRE(retval != 0);
    REQUIRE(outputs.size() == 0);
}

// TODO: write more test cases

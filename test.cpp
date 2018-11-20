
/**
 * Testing for the subprocess library.
 * Uses the Catch2 testing library (https://github.com/catchorg/Catch2)
 */

#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include "catch.hpp"

#include "subprocess.hpp"

/* TODO: test all permuations of arguments */
TEST_CASE("[iterable] basic echo execution", "[subprocess::execute]") {
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    subprocess::execute("/bin/echo", {"hello"}, inputs, [&](std::string s) { outputs.push_back(s); });
    REQUIRE(outputs.size() == 1);
    // echo appends a newline by default
    REQUIRE(outputs.front() == "hello\n");
}

TEST_CASE("[iterable] no trailing output newline echo execution", "[subprocess::execute]") {
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    subprocess::execute("/bin/echo", {"-n", "hello"}, inputs, [&](std::string s) { outputs.push_back(s); });

    REQUIRE(outputs.size() == 1);
    REQUIRE(outputs.front() == "hello");
}

TEST_CASE("[iterable] non existent executable", "[subprocess::execute]") {
    // try and run a non-existent executable, what should happen..?
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/wangwang", {}, inputs,
            [](std::string) { FAIL("this functor should never have been called"); });

    // process should have failed..?
    REQUIRE(retval != 0);
    REQUIRE(outputs.size() == 0);
}

TEST_CASE("[iterable] stdin execute simple cat", "[subprocess::execute]") {
    std::list<std::string> inputs = {"henlo wurld\n", "1,2,3,4\n"};
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/cat", {}, inputs, [&](std::string s) { outputs.push_back(s); });

    REQUIRE(retval == 0);
    REQUIRE(outputs.size() == 2);
    REQUIRE(outputs.at(0) == "henlo wurld\n");
    REQUIRE(outputs.at(1) == "1,2,3,4\n");
}

TEST_CASE("[iterable] stdin execute simple cat no trailing newline for last input", "[subprocess::execute]") {
    // executing a command with the last one missing a newline still should work the same, as the stdin stream gets closed.
    std::list<std::string> inputs = {"henlo wurld\n", "1,2,3,4"};
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/cat", {}, inputs, [&](std::string s) { outputs.push_back(s); });

    REQUIRE(retval == 0);
    REQUIRE(outputs.size() == 2);
    REQUIRE(outputs.at(0) == "henlo wurld\n");
    REQUIRE(outputs.at(1) == "1,2,3,4");
}

TEST_CASE("[iterator] basic echo execution", "[subprocess::execute]") {
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> args = {"hello"};
    std::vector<std::string> env = {};
    int status;

    status = subprocess::execute("/bin/echo", args.begin(), args.end(), inputs.begin(), inputs.end(), [&](std::string s) { outputs.push_back(s); }, env.begin(), env.end());
    REQUIRE(outputs.size() == 1);
    // echo appends a newline by default
    REQUIRE(outputs.front() == "hello\n");

    outputs.clear();
    status = subprocess::execute("/bin/echo", args.begin(), args.end(), inputs.begin(), inputs.end(), [&](std::string s) { outputs.push_back(s); });
    REQUIRE(outputs.size() == 1);
    // echo appends a newline by default
    REQUIRE(outputs.front() == "hello\n");

    status = subprocess::execute("/bin/echo", args.begin(), args.end(), inputs.begin(), inputs.end()) ;
    REQUIRE(status == 0);

    status = subprocess::execute("/bin/echo", args.begin(), args.end(), inputs.begin(), inputs.end()) ;
    REQUIRE(status == 0);

    status = subprocess::execute("/bin/echo", args.begin(), args.end());
    REQUIRE(status == 0);

    status = subprocess::execute("/bin/echo");
    REQUIRE(status == 0);
}

/* TODO: make the rest iterators */
TEST_CASE("[iterator] no trailing output newline echo execution", "[subprocess::execute]") {
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    subprocess::execute("/bin/echo", {"-n", "hello"}, inputs, [&](std::string s) { outputs.push_back(s); });

    REQUIRE(outputs.size() == 1);
    REQUIRE(outputs.front() == "hello");
}

TEST_CASE("[iterator] non existent executable", "[subprocess::execute]") {
    // try and run a non-existent executable, what should happen..?
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/wangwang", {}, inputs,
            [](std::string) { FAIL("this functor should never have been called"); });

    // process should have failed..?
    REQUIRE(retval != 0);
    REQUIRE(outputs.size() == 0);
}

TEST_CASE("[iterator] stdin execute simple cat", "[subprocess::execute]") {
    std::list<std::string> inputs = {"henlo wurld\n", "1,2,3,4\n"};
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/cat", {}, inputs, [&](std::string s) { outputs.push_back(s); });

    REQUIRE(retval == 0);
    REQUIRE(outputs.size() == 2);
    REQUIRE(outputs.at(0) == "henlo wurld\n");
    REQUIRE(outputs.at(1) == "1,2,3,4\n");
}

TEST_CASE("[iterator] stdin execute simple cat no trailing newline for last input", "[subprocess::execute]") {
    // executing a command with the last one missing a newline still should work the same, as the stdin stream gets closed.
    std::list<std::string> inputs = {"henlo wurld\n", "1,2,3,4"};
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/cat", {}, inputs, [&](std::string s) { outputs.push_back(s); });

    REQUIRE(retval == 0);
    REQUIRE(outputs.size() == 2);
    REQUIRE(outputs.at(0) == "henlo wurld\n");
    REQUIRE(outputs.at(1) == "1,2,3,4");
}


TEST_CASE("checkOutput simple case cat", "[subprocess::checkOutput]") {
    // execute bc and pass it some equations
    std::list<std::string> inputs = {"1+1\n", "2^333\n", "32-32\n"};
    // this one is interesting, there's more than one line of stdout for each input (bc line breaks after a certain number of characters)
    std::vector<std::string> output_expected = {"2\n", "17498005798264095394980017816940970922825355447145699491406164851279\\\n", "623993595007385788105416184430592\n", "0\n"};
    std::vector<std::string> out = subprocess::check_output("/usr/bin/bc", {}, inputs);

    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);
}

/* tests pending API update */
//TEST_CASE("asynchronous is actually asynchronous", "[subprocess::async]") {
//    std::list<std::string> inputs;
//    std::vector<std::string> outputs;
//
//    std::atomic<bool> isDone(false);
//    std::future<int> retval = subprocess::async("/usr/bin/time", {"sleep", "3"}, inputs, [&](std::string s) { isDone.store(true); outputs.push_back(s); });
//    // reaching here after the async starts, means that we prrroooooobbbaabbbly (unless the user has a very, very slow computer) won't be finished
//    REQUIRE(isDone.load() == false);
//    REQUIRE(retval.get() == 0);
//
//    // time has different outputs for different OSes, pluuus they will take different times to complete. all we need is some stdout.
//    REQUIRE(outputs.size() > 0);
//}
//
//TEST_CASE("output iterator contains everything", "[subprocess::ProcessStream]") {
//    // stream output from a process
//    std::list<std::string> inputs = {"12232\n", "hello, world\n", "Hello, world\n", "line: Hello, world!\n"};
//    subprocess::ProcessStream ps("/bin/grep", {"-i", "^Hello, world$"}, inputs);
//    std::vector<std::string> expectedOutput = {"hello, world\n", "Hello, world\n"};
//    std::vector<std::string> outputs;
//    for (std::string out : ps) {
//        outputs.push_back(out);
//    }
//
//    REQUIRE(outputs == expectedOutput);
//}
//
//TEST_CASE("output iterator handles empty output", "[subprocess::ProcessStream]") {
//    std::list<std::string> inputs = {"12232\n", "hello, world\n", "Hello, world\n", "line: Hello, world!\n"};
//    subprocess::ProcessStream ps("/bin/grep", {"-i", "^bingo bango bongo$"}, inputs);
//    std::vector<std::string> expectedOutput = {};
//    std::vector<std::string> outputs;
//    for (std::string out : ps) {
//        FAIL("why do we have output!!! - " << out);
//        outputs.push_back(out);
//    }
//
//    REQUIRE(outputs == expectedOutput);
//}
//
//TEST_CASE("output iterator all operator overload testing", "[subprocess::ProcessStream]") {
//    // stream output from a process
//    std::list<std::string> inputs = {"12232\n", "hello, world\n", "Hello, world\n", "line: Hello, world!\n"};
//    subprocess::ProcessStream ps("/bin/grep", {"-i", "Hello, world"}, inputs);
//    std::list<std::string> expectedOutput = {"hello, world\n", "Hello, world\n", "line: Hello, world!\n"};
//
//    auto beg = ps.begin();
//    auto end = ps.end();
//
//    REQUIRE(beg != end);
//
//    REQUIRE(*beg == expectedOutput.front());
//    expectedOutput.pop_front();
//    REQUIRE(beg != end);
//
//    ++beg;
//    REQUIRE(*beg == expectedOutput.front());
//    expectedOutput.pop_front();
//    REQUIRE(beg != end);
//
//    beg++;
//    REQUIRE(*beg == expectedOutput.front());
//    expectedOutput.pop_front();
//
//    beg++;
//    REQUIRE(beg == end);
//    REQUIRE(expectedOutput.size() == 0);
//}

// TODO: write more test cases (this seems pretty covering, let's see how coverage looks)

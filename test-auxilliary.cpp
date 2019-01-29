#include "catch.hpp"
#include "subprocess.hpp"

/*
 * Some additional tests I'd like/things to improve:
 *  - TODO: replace these system binaries with hand written ones in test_programs, 
 *          so we can ensure these are all available across all systems.
 *  - TODO: add some more tests about lots of output generated for little inputs
 *  - TODO: add some tests which will fill up the pipe fully (https://unix.stackexchange.com/questions/11946/how-big-is-the-pipe-buffer)
 *  - TODO: should a process be started in the dtor if it hasn't already been..?
 */

TEST_CASE("[iterable] basic echo execution", "[subprocess::execute]") {
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    subprocess::execute("/bin/echo", {"hello"}, inputs, [&](std::string s) { outputs.push_back(s); });
    REQUIRE(outputs.size() == 1);
    // echo appends a newline by default
    REQUIRE(outputs.front() == "hello\n");
}

TEST_CASE("[iterable] basic echo execution varargs", "[subprocess::execute]") {
    // test that execute will compile file with vargs
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> env = {"LOL=lol"};
    subprocess::execute("/bin/echo", {"hello"}, inputs, [&](std::string s) { outputs.push_back(s); }, env);
    REQUIRE(outputs.size() == 1);
    // echo appends a newline by default
    REQUIRE(outputs.front() == "hello\n");

    outputs.clear();
    int status =
            subprocess::execute("/bin/echo", {"hello"}, inputs, [&](std::string s) { outputs.push_back(s); });
    REQUIRE(outputs.size() == 1);
    REQUIRE(status == 0);

    // XXX: this causes a linker error..? well.. it used to.
    // I think I need to fix the template for the Iterator one, to be more strict in what it accepts (i think it might actually be able to cast some Iterables to Iterators)
    outputs.clear();
    status = subprocess::execute("/bin/echo", {"hello"}, inputs);
    REQUIRE(status == 0);

    outputs.clear();
    status = subprocess::execute("/bin/echo", {"hello"});
    REQUIRE(status == 0);

    outputs.clear();
    status = subprocess::execute("/bin/echo");
    REQUIRE(status == 0);
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
    // executing a command with the last one missing a newline still should work the same, as the stdin stream
    // gets closed.
    std::list<std::string> inputs = {"henlo wurld\n", "1,2,3,4"};
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/cat", {}, inputs, [&](std::string s) { outputs.push_back(s); });

    REQUIRE(retval == 0);
    REQUIRE(outputs.size() == 2);
    REQUIRE(outputs.at(0) == "henlo wurld\n");
    REQUIRE(outputs.at(1) == "1,2,3,4");
}

TEST_CASE("[iterable] test env variables are sent to program correctly", "[subprocess::execute]") {
    // executing a command with the last one missing a newline still should work the same, as the stdin stream
    // gets closed.
    std::vector<std::string> outputs;
    int retval = subprocess::execute("./test_programs/print_env", {"LOL"}, {},
            [&](std::string s) { outputs.push_back(s); }, {"LOL=lol"});

    REQUIRE(retval == 0);
    REQUIRE(outputs.size() == 1);
    REQUIRE(outputs.at(0) == "LOL,lol\n");
}

TEST_CASE("[iterator] basic echo execution", "[subprocess::execute]") {
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<std::string> args = {"hello"};
    std::vector<std::string> env = {};
    int status;

    status = subprocess::execute("/bin/echo", args.begin(), args.end(), inputs.begin(), inputs.end(),
            [&](std::string s) { outputs.push_back(s); }, env.begin(), env.end());
    REQUIRE(outputs.size() == 1);
    // echo appends a newline by default
    REQUIRE(outputs.front() == "hello\n");

    // test the optional arguments compile
    outputs.clear();
    status = subprocess::execute("/bin/echo", args.begin(), args.end(), inputs.begin(), inputs.end(),
            [&](std::string s) { outputs.push_back(s); });
    REQUIRE(outputs.size() == 1);
    // echo appends a newline by default
    REQUIRE(outputs.front() == "hello\n");

    status = subprocess::execute("/bin/echo", args.begin(), args.end(), inputs.begin(), inputs.end());
    REQUIRE(status == 0);

    status = subprocess::execute("/bin/echo", args.begin(), args.end(), inputs.begin(), inputs.end());
    REQUIRE(status == 0);

    status = subprocess::execute("/bin/echo", args.begin(), args.end());
    REQUIRE(status == 0);

    status = subprocess::execute("/bin/echo");
    REQUIRE(status == 0);
}

TEST_CASE("[iterator] no trailing output newline echo execution", "[subprocess::execute]") {
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    subprocess::execute("/bin/echo", {"-n", "hello"}, inputs, [&](std::string s) { outputs.push_back(s); });

    REQUIRE(outputs.size() == 1);
    REQUIRE(outputs.front() == "hello");
}

TEST_CASE("[iterator] non existent executable", "[subprocess::execute]") {
    // try and run a non-existent executable, what should happen..?
    std::list<std::string> args;
    std::list<std::string> inputs;
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/wangwang", args.begin(), args.end(), inputs.begin(), inputs.end(),
            [](std::string) { FAIL("this functor should never have been called"); });

    // process should have failed..?
    REQUIRE(retval != 0);
    REQUIRE(outputs.size() == 0);
}

TEST_CASE("[iterator] stdin execute simple cat", "[subprocess::execute]") {
    std::list<std::string> args;
    std::list<std::string> inputs = {"henlo wurld\n", "1,2,3,4\n"};
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/cat", args.begin(), args.end(), inputs.begin(), inputs.end(),
            [&](std::string s) { outputs.push_back(s); });

    REQUIRE(retval == 0);
    REQUIRE(outputs.size() == 2);
    REQUIRE(outputs.at(0) == "henlo wurld\n");
    REQUIRE(outputs.at(1) == "1,2,3,4\n");
}

TEST_CASE("[iterator] stdin execute simple cat no trailing newline for last input", "[subprocess::execute]") {
    // executing a command with the last one missing a newline still should work the same, as the stdin stream
    // gets closed.
    std::list<std::string> args;
    std::list<std::string> inputs = {"henlo wurld\n", "1,2,3,4"};
    std::vector<std::string> outputs;
    int retval = subprocess::execute("/bin/cat", args.begin(), args.end(), inputs.begin(), inputs.end(),
            [&](std::string s) { outputs.push_back(s); });

    REQUIRE(retval == 0);
    REQUIRE(outputs.size() == 2);
    REQUIRE(outputs.at(0) == "henlo wurld\n");
    REQUIRE(outputs.at(1) == "1,2,3,4");
}

TEST_CASE("[iterable] check_output simple case bc", "[subprocess::check_output]") {
    // execute bc and pass it some equations
    std::list<std::string> inputs = {"1+1\n", "2^333\n", "32-32\n"};
    // this one is interesting, there's more than one line of stdout for each input (bc line breaks after a
    // certain number of characters)
    std::vector<std::string> output_expected = {"2\n",
            "17498005798264095394980017816940970922825355447145699491406164851279\\\n",
            "623993595007385788105416184430592\n", "0\n"};
    std::vector<std::string> out = subprocess::check_output("/usr/bin/bc", {}, inputs);

    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);
}

TEST_CASE("[iterable] check_output permutations (varargs)", "[subprocess::check_output]") {
    // execute echo over a series of lines
    std::list<std::string> inputs = {"line1\n", "line2\n", "line3\n"};
    std::list<std::string> env = {"LOL=lol"};
    std::vector<std::string> output_expected = {"line1\n", "line2\n", "line3\n"};
    std::vector<std::string> out;

    out.clear();
    out = subprocess::check_output("/bin/cat", {}, inputs, env);
    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);

    out.clear();
    out = subprocess::check_output("/bin/cat", {}, inputs);
    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);

    // now do the same with echo (as we're limited to args only at this point)
    out.clear();
    std::vector<std::string> args = {"value"};
    output_expected = {"value\n"};
    out = subprocess::check_output("/bin/echo", args);
    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);

    // and echo without any args just outputs a blank line
    out.clear();
    output_expected = {"\n"};
    out = subprocess::check_output("/bin/echo");
    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);
}

TEST_CASE("[iterator] check_output simple case bc", "[subprocess::check_output]") {
    std::vector<std::string> args;
    // execute bc and pass it some equations
    std::list<std::string> inputs = {"1+1\n", "2^333\n", "32-32\n"};
    // this one is interesting, there's more than one line of stdout for each input (bc line breaks after a
    // certain number of characters)
    std::vector<std::string> output_expected = {"2\n",
            "17498005798264095394980017816940970922825355447145699491406164851279\\\n",
            "623993595007385788105416184430592\n", "0\n"};
    std::vector<std::string> out = subprocess::check_output("/usr/bin/bc", args.begin(), args.end(), inputs.begin(), inputs.end());

    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);
}

TEST_CASE("[iterator] check_output permutations (varargs)", "[subprocess::check_output]") {
    // execute echo over a series of lines
    std::deque<std::string> args; 
    std::list<std::string> inputs = {"line1\n", "line2\n", "line3\n"};
    std::list<std::string> env = {"LOL=lol"};
    std::vector<std::string> output_expected = {"line1\n", "line2\n", "line3\n"};
    std::vector<std::string> out;

    out.clear();
    out = subprocess::check_output("/bin/cat", args.begin(), args.end(), inputs.begin(), inputs.end(), env.begin(), env.end());
    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);

    out.clear();
    out = subprocess::check_output("/bin/cat", args.begin(), args.end(), inputs.begin(), inputs.end());
    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);

    // now do the same with echo (as we're limited to args only at this point)
    out.clear();
    args = {"value"};
    output_expected = {"value\n"};
    out = subprocess::check_output("/bin/echo", args.begin(), args.end());
    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);

    // and echo without any args just outputs a blank line
    out.clear();
    output_expected = {"\n"};
    out = subprocess::check_output("/bin/echo");
    REQUIRE(out.size() == output_expected.size());
    REQUIRE(out == output_expected);
}



// TODO: make all these have timeouts! it's possible that they never terminate
// TODO: somehow ensure that if we try and retrieve more output it fails..? idk, seems annoying
//       perhaps we just use the timeouts, with some reasonable duration..?
// TODO: replace these tests as I made having the functor AND being able to extract stdout illegal

TEST_CASE("basic process instantiation", "[subprocess::Process]") {
    subprocess::Process p("/bin/echo", {"henlo world"});

    p.start();
    std::string line;
    p >> line;

    REQUIRE(line == "henlo world\n");
}


// handy deferrable functions (executed on dtor)
template <typename Functor>
struct Deferrable {
    Functor func;
    Deferrable(Functor f) : func(f) {}
    ~Deferrable() {
        func();
    }
};

TEST_CASE("process functor", "[subprocess::Process]") {

    // requirement from the dead 
    // just ensure that even after the dtor, the functor isn't invoked again!
    std::string line;
    size_t func_count = 0;
    // as the dtors are invoked in a stack like matter, this fn will be checked after the 
    // process' termination
    auto deferred_assertion = Deferrable<std::function<void()>>([&]() {
        REQUIRE(func_count == 1);
    });

    subprocess::Process p("/bin/echo", {"henlo world"}, [&](std::string s) {
        func_count += 1;
        REQUIRE(s == "henlo world\n");
            });

    p.start();
    p.finish();

    REQUIRE(func_count == 1);
}

TEST_CASE("pre-emptive process input", "[subprocess::Process]") {
    subprocess::Process p("/bin/cat");

    p << "henlo world\n";
    p.start();

    std::string line;
    p >> line;

    REQUIRE(line == "henlo world\n");
}

/*
TEST_CASE("post process start input", "[subprocess::Process]") {
    subprocess::Process p("/bin/cat");

    p.start();

    p << "henlo world\n";

    std::string line;
    p >> line;

    REQUIRE(line == "henlo world\n");
}
*/

/*
TEST_CASE("reading from process that itself is a successor proc", "[subprocess::Process]") {
    // TODO: add timeout
    subprocess::Process p1("/bin/echo", {"high to roam"});
    subprocess::Process p2("/bin/grep", {"-o", "hi"});

    p1.pipe_to(p2);
    
    p1.start();

    std::string line;
    // XXX: this line is currently making the test hang. The reason is simple, but the implications are complex.
    // When reading from p2, there isn't a line instantly available, as it requires input from p1 
    //  - but we don't currently call readline in the predecessor. If we do, what if the current
    //  process *will* output, but is taking a while..?
    p2 >> line;

    REQUIRE(line == "hi\n");
}

TEST_CASE("malordered process RAII", "[subprocess::Process]") {
    bool func_called = false;
    auto deferred_assertion = Deferrable<std::function<void()>>([&]() {
        REQUIRE(func_called == true);
    });
    // test that initialising the processes in the reverse stack order won't bork them
    subprocess::Process p2("/bin/grep", {"-o", "hi"}, [&](std::string s) {
            REQUIRE(s == "hi\n");
            func_called = true;
            });
    subprocess::Process p1("/bin/echo", {"high to roam"});

    p1.pipe_to(p2);

    p1.start();
}

TEST_CASE("RAII doesn't start non-started process", "[subprocess:Process]") {
    subprocess::Process p1("/bin/echo", {"die bart die"}, [&](std::string) {
            FAIL("process output when shouldn't have");
            });
}
*/

TEST_CASE("superfluous input", "[subprocess::Process]") {
    // provide input to a process that won't use it.
}

TEST_CASE("", "[subprocess::Process]") {

}

TEST_CASE("reading from succesor plus functor", "[subprocess::Process]") {

}

TEST_CASE("multi-line output", "[subprocess::Process]") {
    // test a process that outputs lots of output for each line of stdin

}

TEST_CASE("post-start manual process input", "[subprocess::Process]") {

}

TEST_CASE("simple process piping", "[subprocess::Process]") {

}

TEST_CASE("piping to file", "[subprocess::Process]") {

}

TEST_CASE("bifurcated file outputting", "[subprocess::Process]") {

}

TEST_CASE("long pipe chain", "[subprocess::Process]") {

}

TEST_CASE("complex process piping", "[subprocess::Process]") {

}

TEST_CASE("cyclic process piping", "[subprocess::Process]") {
    // TODO: fail if this takes too long, that indicates there's a problem 
    // probably will be the next prime implementation
}

TEST_CASE("test infinite output cropped via head unix pipe", "[subprocess::Process]") {

}

TEST_CASE("", "[subprocess::Process]") {

}

TEST_CASE("", "[subprocess::Process]") {

}

TEST_CASE("", "[subprocess::Process]") {

}

TEST_CASE("test output iterator", "[subprocess::Process]") {

}

TEST_CASE("test ctor vargs", "[subprocess::Process]") {

}

// TODO: write more test cases (this seems pretty covering, let's see how coverage looks)

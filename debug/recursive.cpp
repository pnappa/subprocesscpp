#include "subprocess.hpp"

// handy deferrable functions (executed on dtor)
template <typename Functor>
struct Deferrable {
    Functor func;
    Deferrable(Functor f) : func(f) {}
    ~Deferrable() {
        func();
    }
};

// class that enforces a timeout to fail the test
using namespace std::chrono_literals;
struct Timeout {
    std::chrono::milliseconds c_duration = 0ms;
    std::chrono::milliseconds total_time;
    bool stopped = false;
    std::thread waiter;

    Timeout(std::chrono::milliseconds timeout = 3000ms) :
        total_time(timeout),
        waiter(std::thread([&]() {
                    auto start = std::chrono::high_resolution_clock::now();
                    while (c_duration <= total_time) {
                    std::this_thread::sleep_for(1ms);
                    c_duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start);
                    if (stopped) return;
                    }
                    throw new std::runtime_error("timed out");
                    })) {}

    ~Timeout() {
        stopped = true;
        waiter.join();
    }
};


int main() {
    // uncomment if you want to throw an exception if it takes too long
    //Timeout t(1000ms);
    subprocess::Process p1("/bin/echo", {"high to roam"});
    subprocess::Process p2("/bin/grep", {"-o", "hi"});

    p1.pipe_to(p2);
    
    p1.start();

    std::string line;
    p2 >> line;
    std::cout << line;
}

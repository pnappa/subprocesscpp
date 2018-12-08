/** 
 * Demonstration of recursive subprocess pipes
 *
 * This program find the next prime for an input number.
 */


#include "subprocess.hpp"

int main() {
    subprocess::Process incrementer("./test_programs/increment", {}, [&](std::string s) { std::cout << s << std::endl; });
    subprocess::Process prime_checker("./test_programs/tee_if_nonprime");

    incrementer.pipe_to(prime_checker);
    prime_checker.pipe_to(incrementer);
    prime_checker.output_to_file("prime.out");

    incrementer.start();
    incrementer << "33\n";

    prime_checker.finish();
}


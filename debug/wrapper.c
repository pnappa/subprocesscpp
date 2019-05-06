#define _POSIX_C_SOURCE 200809
#include <signal.h>
#include <stdatomic.h>
#include <unistd.h>
#include <threads.h>
#include <stdio.h>
#include <stdbool.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>

/**
 * This is me testing an prototype version which wraps the subprocess with another subprocess
 * So that we can catch sigchld and nicely handle the lifetimes of the processes.
 *
 * XXX: note, you probably have to compile with musl_gcc, as threads.h isn't easy to find
 */

// communicating lifetime of the wrangler (the wrangler writes, the parent reads)
int lifetimeComms[2];
// the parent will tell the wrangler to die
int killComms[2];
// stdin, stdout
int to_child[2];
int from_child[2];
atomic_bool wranglerFinished;

// called when signal occurs
void closure(int signum) {
    // write something to lifetimeComms[1]
    char* out = (char*) "ping";
    write(lifetimeComms[1], out, strlen(out));

    // TODO: close stdouts?
}

// conv to null terminated array
char* const * conv_args(int argc, char* argv[]) {
    char** ret = malloc(sizeof(char*) * (argc+1));
    for (int i = 0; i < argc; ++i) {
        /*ret[i] = strndup(argv[i], strlen(argv[i]));*/
        ret[i] = argv[i];
    }
    ret[argc] = NULL;

    return ret;
}

// called by the main process' thread, which reads from stdin, and forwards to the subsubprocess
int pumpdata(void* arg) {
    char buffer[1024];
    FILE* reader = fdopen(from_child[0], "r");
    while (!atomic_load(&wranglerFinished)) {
        // read a line of input from the user
        char* res = fgets(buffer, 1023, stdin);
        // close stdin 
        if (!res) {
            close(to_child[1]);
            break;
        }
        // write to pipe
        write(to_child[1], buffer, strlen(buffer));

        // we'll expect a pinged line back from cat 
        fgets(buffer, 1023, reader);
        printf("readline: %s\n", buffer);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("usage: %s program [args...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    atomic_store(&wranglerFinished, false);

    bool failure = pipe(lifetimeComms) < 0;
    failure |= pipe(killComms) < 0;
    failure |= pipe(to_child) < 0;
    failure |= pipe(from_child) < 0;
    // TODO: test failure

    pid_t pid = fork();
    // launch the wrapper
    if (pid == 0) {
        close(to_child[1]);
        close(from_child[0]);
        close(lifetimeComms[0]);
        close(killComms[1]);

        pid_t subproc_pid = fork();
        if (subproc_pid == 0) {
            dup2(to_child[0], STDIN_FILENO);
            dup2(from_child[1], STDOUT_FILENO);
            // memleak, but who cares, we're just testing
            execvp(argv[1], conv_args(argc-1, argv+1));
            // reach here? something borked
            exit(EXIT_FAILURE);
        } else {
            signal(SIGCHLD, closure);
            // TODO: push stdin and pull stdout to/from our exec'd program
            // ^^ dunno if i need to anymore, i've got the parent and the subsubprocess directly talking now.
            
            // wait for permission to die
            struct pollfd fds = {killComms[0], POLLIN, 0};
            int res = poll(&fds, 1, -1);

            close(to_child[0]);
            close(from_child[1]);
            close(lifetimeComms[1]);
            close(killComms[0]);
            // TODO: handle res of the close and polling

            exit(EXIT_SUCCESS);
        }
    } else {
        // don't need to write to lifeline
        close(lifetimeComms[1]);
        // we tell them to die
        close(killComms[0]);

        // close the reading end of stdin
        close(to_child[0]);
        // close the writing end of stdout
        close(from_child[1]);

        thrd_t pumper;
        int create_suc = thrd_create(&pumper, pumpdata, NULL);
        int thread_res;

        struct pollfd fds = {lifetimeComms[0], POLLIN, 0};
        // if information comes from the comms pipe, it means that the sub-subprocess is finished.
        int res = poll(&fds, 1, -1);
        printf("poll res: %d\n", res);
        // TODO: close stuff?
        // test res, then set atomic
        atomic_store(&wranglerFinished, true);

        thrd_join(pumper, &thread_res);
        // let the wrangler know it can die now
        char* out = (char*) "ping";
        write(killComms[1], out, strlen(out));
    }
}

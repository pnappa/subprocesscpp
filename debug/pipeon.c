#define _POSIX_C_SOURCE 200809
#include <signal.h>
#include <stdatomic.h>
#include <unistd.h>
#include <threads.h>
#include <assert.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdbool.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>

/**
 * Now this one tests me manually piping one process to the next, and catching sigchld
 *
 * XXX: note, you probably have to compile with musl_gcc, as threads.h isn't easy to find
 */

#define DEFAULT_SUCCS 10

// TODO: perhaps know what process is next so we can waterfall closures?
struct process_comms {
    // stdin, stdout
    int to_child[2];
    int from_child[2];
    pid_t pid;

    // this process can have multiple successors
    int num_succs;
    // amount of space in the successors array
    int capacity;
    struct process_comms** successors;

    // XXX: add the concept of predecessors, as we need to know whether all inputs to a prog
    // have closed?
};

// TBH what's the point of this
// just a doubly linked list
struct active_processes {
    struct active_processes* prev;
    struct active_processes* next;

    struct process_comms payload;
};

/* append succ to the successor of parent, resizing if necessary */
void add_successor(struct process_comms* parent, struct process_comms* succ) {
    if (parent->num_succs == parent->capacity) {
        parent->capacity *= 2;
        parent->successors = realloc(parent->successors, sizeof(struct process_comms*) * parent->capacity);
    }

    parent->successors[parent->num_succs++] = succ;
}

// XXX: not threadsafe, how does this work?
struct active_processes* root = NULL;

// called when signal occurs
void process_closure(int signum) {
    assert(signum == SIGCHLD);

    // go through and find the matching process(es) [signal can catch multiple at once]
    // ensure that we have all children close events
    while (true) {
        int status;
        pid_t captured_pid = waitpid(-1, &status, WNOHANG);
        if (captured_pid <= 0) break;

        // find the pid and remove it from the linked list
        for (struct active_processes* curr = root; curr != NULL; curr = curr->next) {
            if (curr->payload.pid == captured_pid) {
                // XXX: what else was i supposed to do, can't remember
                // maybe close output, and indicate the next should close?
                close(curr->payload.to_child[1]);
                // TODO: err handling ^
                // TODO: perhaps withdraw all remaining info? i guess that's fine
                // TODO: perhaps send EOF to next proc? idk
        
                // remove from linked list
                if (curr->prev != NULL) {
                    curr->prev->next = curr->next;
                    if (curr->next) curr->next->prev = curr->prev;
                }

                struct active_processes* tmp = curr;
                // ensure that the loop will continue properly
                curr = curr->prev;
                // XXX: err, this doesn't seem right, I think we actually want to keep it around
                // then when the stdout is closed, we can close the stdin for the next prog
                free(tmp);
                
                break;
            }
        }
    }
}

/*
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
*/

/* make the process and add it into the linked list */
struct process_comms* make_process(char* const * program) {
    struct process_comms* proc = malloc(sizeof(struct process_comms));
    proc->capacity = DEFAULT_SUCCS;
    proc->successors = malloc(sizeof(struct process_comms*)* proc->capacity);
    proc->num_succs = 0;

    bool res = pipe(proc->to_child) < 0;
    res |= pipe(proc->from_child) < 0;

    // setup for us to close any processes' pipes when they die
    signal(SIGCHLD, process_closure);

    pid_t pid = fork();
    // our process to exec
    if (pid == 0) {
        close(proc->to_child[1]);
        close(proc->from_child[0]);
        dup2(proc->to_child[0], STDIN_FILENO);
        dup2(proc->from_child[1], STDOUT_FILENO);

        // TODO fcntl?
        execvp(program[0], program);
        // reach here? something borked
        exit(EXIT_FAILURE);
    } else {
        proc->pid = pid;
        // the parent needs to close the ends
        // close the reading end of stdin
        close(proc->to_child[0]);
        // close the writing end of stdout
        close(proc->from_child[1]);
    }

    return proc;
}

// thread target to pass information around
void connect_process(struct process_comms* p1, struct process_comms* p2) {
    // some kind of loop to read from p1's output into p2's input
    // Note, of course we could just dup2 to make p1 write directly to p2, but
    // we want to support multigraphs, so that's not feasible
    // this function will be replaced later, adding extra fields to process_comms,
    // where that processes' thread will pump output to that all "successors"
}


int main(int argc, char* argv[]) {
    char* prog1[] = {"/bin/echo", "burgers are highly regarded", NULL};
    char* prog2[] = {"/bin/grep", "-o", "hi", NULL};

    struct process_comms* proc1 = make_process(prog1);
    struct process_comms* proc2 = make_process(prog2);

    // connect echo to grep
    add_successor(proc1, proc2);
    // can even have this too, to forward output
    add_successor(proc1, proc2);

    const int buflen = 1024;
    char buffer[buflen];
    FILE* p1_reader = fdopen(proc1->from_child[0], "r");
    char* res;
    while ((res = fgets(buffer, buflen - 1, p1_reader))) {
        int output_len = strlen(buffer);
        for (int i = 0; i < proc1->num_succs; ++i) {
            write(proc1->successors[i]->to_child[1], buffer, output_len);
        }
    }

    // now let's be lazy and just read output from each of the successors of proc1
    // this would be done in a thread, i suppose
    // we would want to drain the output of the pipe as otherwise after like 65k it gets clogged
    for (int i = 0; i < proc1->num_succs; ++i) {
        FILE* succ_reader = fdopen(proc1->successors[i]->from_child[0], "r");
        // we assume that all the predecessors for this are closed, in a future version we'll
        // make sure.
        close(proc1->successors[i]->to_child[1]);
        while ((res = fgets(buffer, buflen - 1, succ_reader))) {
            printf("%s", buffer);
        }
    }

    /*
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
    */
}

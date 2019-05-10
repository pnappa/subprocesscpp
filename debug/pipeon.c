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
// v this isn't thread safe too :v , so i've switched to an actual atomic
/*volatile sig_atomic_t needs_signal_cleanup = 0;*/
atomic_bool needs_signal_cleanup;
atomic_bool run_waiter;

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

    // as we need to know whether all inputs to a prog have closed?
    int num_preds;
    
    // whether we should ignore this in the cleanup
    bool closed;
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
    succ->num_preds++;
}

// XXX: not threadsafe, how does this work?
struct active_processes* root = NULL;

// called when signal occurs
void process_closure(int signum) {
    assert(signum == SIGCHLD);
    // XXX: hmm, we probably should just flip a signal here?
    atomic_store(&needs_signal_cleanup, true);
}

/* make the process and add it into the linked list */
struct process_comms* make_process(char* const * program) {
    struct process_comms* proc = malloc(sizeof(struct process_comms));
    proc->capacity = DEFAULT_SUCCS;
    proc->successors = malloc(sizeof(struct process_comms*)* proc->capacity);
    proc->num_succs = 0;
    proc->num_preds = 0;
    proc->closed = false;

    bool res = pipe(proc->to_child) < 0;
    res |= pipe(proc->from_child) < 0;

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

/* close this process, potentially recursively closing others if the successors for this one
 * have all predecessors closed
 * closing means to close the stdin. */
void close_proc(struct process_comms* proc) {
    // awkward recursive closure case?
    if (proc->closed) return;

    // close the stdin (this may either be a terminated process, or a running process)
    close(proc->to_child[1]);
    proc->closed = true;

    return;
    for (int i = 0; i < proc->num_succs; ++i) {
        proc->successors[i]->num_preds--;
        if (proc->successors[i]->num_preds == 0) {
            // close this one too, as it has no predecessors remaining.
            close_proc(proc->successors[i]);
        }
    }
}

/* a long running thread that checks whether there are signals and deals with them */
int proc_waiter(void* arg) {
    // silence IDE's warning, bleh
    (void) (void*) arg;

    while (atomic_load(&run_waiter)) {
        // XXX: switch to weak? we're in a loop
        // use CAS to toggle needs_signal_cleanup to false if it's true, and enter the conditional
        bool does_need_cleanup = true;
        if (atomic_compare_exchange_strong(&needs_signal_cleanup, &does_need_cleanup, false)) {

            // go through and find the matching process(es) [signal can catch multiple at once]
            // ensure that we have all children close events
            while (true) {
                int status;
                pid_t captured_pid = waitpid(-1, &status, WNOHANG);
                if (captured_pid <= 0) break;

                // find the pid and remove it from the linked list
                for (struct active_processes* curr = root; curr != NULL; curr = curr->next) {

                    if (curr->payload.pid == captured_pid) {
                        // close this process, and potentially close successors if they're ready
                        close_proc(&curr->payload);

                        // remove from linked list
                        if (curr->prev != NULL) {
                            curr->prev->next = curr->next;
                            if (curr->next) curr->next->prev = curr->prev;
                        }

                        struct active_processes* tmp = curr;
                        // ensure that the loop will continue properly
                        if (curr != NULL) curr = curr->prev;
                        free(tmp);

                        break;
                    }
                }
            }
        }
    }
}

// thread target to pass information around
void connect_process(struct process_comms* p1, struct process_comms* p2) {
    // some kind of loop to read from p1's output into p2's input
    // Note, of course we could just dup2 to make p1 write directly to p2, but
    // we want to support multigraphs, so that's not feasible
    // this function will be replaced later, adding extra fields to process_comms,
    // where that processes' thread will pump output to that all "successors"
}

/* read the output for proc and if it has a successor, pipe to them, otherwise printf */
int pump_output(void* arg) {
    struct process_comms* proc = arg;
    const int buflen = 1024;
    char buffer[buflen];
    FILE* p1_reader = fdopen(proc->from_child[0], "r");
    char* res;
    while ((res = fgets(buffer, buflen - 1, p1_reader))) {
        int output_len = strlen(buffer);
        if (proc->num_succs > 0) {
            for (int i = 0; i < proc->num_succs; ++i) {
                write(proc->successors[i]->to_child[1], buffer, output_len);
            }
        } else {
            printf("%s", buffer);
        }
    }
    return 0;
}


int main(int argc, char* argv[]) {

    // whether the waitpid thread should keep spinning
    atomic_store(&run_waiter, true);
    // setup for us to indicate whether we need to close process' pipes
    signal(SIGCHLD, process_closure);
    
    // thread to lookout for SIGCHLDs
    thrd_t process_waiter;
    int t_suc = thrd_create(&process_waiter, proc_waiter, NULL);
    printf("waiter: %d\n", t_suc);

    char* prog1[] = {"/bin/echo", "burgers are highly regarded", NULL};
    char* prog2[] = {"/bin/grep", "-o", "hi", NULL};

    struct process_comms* proc1 = make_process(prog1);
    struct process_comms* proc2 = make_process(prog2);

    // connect echo to grep
    add_successor(proc1, proc2);
    // can even have this too, to forward output
    /*add_successor(proc1, proc2);*/

    thrd_t proc1_pumper;
    t_suc = thrd_create(&proc1_pumper, pump_output, proc1);
    printf("pumper: %d\n", t_suc);

    thrd_t proc2_pumper;
    t_suc = thrd_create(&proc2_pumper, pump_output, proc1);
    printf("pumper2: %d\n", t_suc);

    // TODO: perhaps have some error handling here, and bloody everywhere
    int res;
    thrd_join(proc1_pumper, &res);
    thrd_join(proc2_pumper, &res);

    atomic_store(&run_waiter, false);
    thrd_join(process_waiter, &res);

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

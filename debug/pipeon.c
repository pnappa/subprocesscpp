#define _POSIX_C_SOURCE 200809
#include <signal.h>
#include <stdatomic.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <assert.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdbool.h>
#include <poll.h>
#include <string.h>
#include <stdlib.h>

/**
 * Testing arbitrary and cycling piping setups.
 *
 * Current logic error is the an EOF can be sent to a successor if the current has closed,
 *  but there may be a situation that current still has output to propagate. Might need
 *  a way to pump output from the close_proc fn.
 *
 * Compile with clang -g -std=c11 pipeon.c -lpthread
 */

#define DEFAULT_SUCCS 10
#define DEBUG true
/*#define DEBUG false */
bool needs_signal_cleanup;
bool run_waiter;

struct process_comms {
    // stdin, stdout
    int to_child[2];
    int from_child[2];
    pid_t pid;

    const char* proc_name;

    // this process can have multiple successors
    int num_succs;
    // amount of space in the successors array
    int capacity;
    struct process_comms** successors;

    // as we need to know whether all inputs to a prog have closed?
    int num_preds;
    
    // whether we should ignore this in the cleanup
    bool closed;

    // whether all output from this process has been forwarded on
    atomic_bool can_close_successors;
};

// chain of the active processes (could do with a better name)
struct active_processes {
    struct active_processes* prev;
    struct active_processes* next;

    struct process_comms* payload;
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

// be sure to use the mutex when reading or writing!
struct active_processes* root = NULL;
pthread_mutex_t linked_list_mut;

// append into the linked list
void append_active_proc(struct process_comms* proc) {
    pthread_mutex_lock(&linked_list_mut);

    if (root == NULL) {
        root = malloc(sizeof(struct active_processes));
        root->prev = NULL;
        root->next = NULL;
        root->payload = proc;
    } else {
        struct active_processes* new = malloc(sizeof(struct active_processes));
        struct active_processes* last = root;
        while (last->next != NULL) last = last->next;
        last->next = new;
        new->prev = last;
        new->next = NULL;
        new->payload = proc;
    }

    pthread_mutex_unlock(&linked_list_mut);
}

// called when signal occurs
void process_closure(int signum) {
    assert(signum == SIGCHLD);
    // XXX: hmm, we probably should just flip a signal here?
    __atomic_store_n(&needs_signal_cleanup, true, __ATOMIC_SEQ_CST);
}

/* make the process and add it into the linked list */
struct process_comms* make_process(char* const * program) {
    struct process_comms* proc = malloc(sizeof(struct process_comms));
    proc->capacity = DEFAULT_SUCCS;
    proc->successors = malloc(sizeof(struct process_comms*)* proc->capacity);
    proc->num_succs = 0;
    proc->num_preds = 0;
    proc->closed = false;
    proc->proc_name = program[0];
    atomic_store(&proc->can_close_successors, false);

    bool res = pipe(proc->to_child) < 0;
    res |= pipe(proc->from_child) < 0;

    pid_t pid = fork();
    // our process to exec
    if (pid == 0) {
        close(proc->to_child[1]);
        close(proc->from_child[0]);
        dup2(proc->to_child[0], STDIN_FILENO);
        dup2(proc->from_child[1], STDOUT_FILENO);

        prctl(PR_SET_PDEATHSIG, SIGTERM);
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
    int res = close(proc->to_child[1]);
    if (DEBUG) printf("close status: %d\n", res); 
    proc->closed = true;

    if (DEBUG) printf("about to close %s\n", proc->proc_name);

    // spin until we can continue. XXX: add some yield?
    while (!atomic_load(&proc->can_close_successors));
    // ^ this logic isn't enough if there are two disjoint processes piping into one.
    // ...actually no, it might be.

    if (DEBUG) printf("closing %s\n", proc->proc_name);

    for (int i = 0; i < proc->num_succs; ++i) {
        struct process_comms* next = proc->successors[i];
        next->num_preds--;
        if (next->num_preds == 0) {
            if (next->closed) continue;
            if (DEBUG) printf("EOF sent to: %s\n", next->proc_name);
            // changed this to be non-recursive...the signal can catch the later ones
            close(next->to_child[1]);

            // close this one too, as it has no predecessors remaining.
            /*close_proc(proc->successors[i]);*/
        }
    }
}

// assumes mutex has been properly obtained
void remove_linked_list_node(struct active_processes* proc) {
    if (proc->prev) {
        proc->prev->next = proc->next;
    }
    if (proc->next) {
        proc->next->prev = proc->prev;
    }

    if (root == proc) {
        root = proc->next;
    }

    free(proc);
}

/* a long running thread that checks whether there are signals and deals with them */
void* proc_waiter(void* arg) {
    // silence IDE's warning, bleh
    (void) (void*) arg;

    while (__atomic_load_n(&run_waiter, __ATOMIC_SEQ_CST)) {
        // XXX: switch to weak? we're in a loop
        // use CAS to toggle needs_signal_cleanup to false if it's true, and enter the conditional
        bool does_need_cleanup = true;
        if (__atomic_compare_exchange_n(&needs_signal_cleanup, &does_need_cleanup, false, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {

            // go through and find the matching process(es) [signal can catch multiple at once]
            // ensure that we have all children close events
            while (true) {
                int status;
                pid_t captured_pid = waitpid(-1, &status, WNOHANG);
                if (captured_pid <= 0) break;
                if (DEBUG) printf("closing pid: %d\n", captured_pid);

                pthread_mutex_lock(&linked_list_mut);
                // find the pid and remove it from the linked list
                struct active_processes* curr = root;
                while (curr != NULL) {

                    if (curr->payload->pid == captured_pid) {
                        // close this process, and potentially close successors if they're ready
                        close_proc(curr->payload);

                        remove_linked_list_node(curr);

                        break;
                    }

                    curr = curr->next;
                }
                pthread_mutex_unlock(&linked_list_mut);
            }
        }
    }

    return NULL;
}

/* read the output for proc and if it has a successor, pipe to them, otherwise printf */
void* pump_output(void* arg) {
    struct process_comms* proc = arg;
    const int buflen = 1024;
    char buffer[buflen];
    FILE* p1_reader = fdopen(proc->from_child[0], "r");
    char* res;
    while ((res = fgets(buffer, buflen - 1, p1_reader))) {
        if (DEBUG) printf("read line for %s: %s", proc->proc_name, buffer);
        int output_len = strlen(buffer);
        if (proc->num_succs > 0) {
            if (DEBUG) printf("piping");
            for (int i = 0; i < proc->num_succs; ++i) {
                write(proc->successors[i]->to_child[1], buffer, output_len);
            }
        } else {
            printf("%s", buffer);
        }
    }

    // let the proc_waiter thread know it can finish
    atomic_store(&proc->can_close_successors, true);
    if (DEBUG) printf("can close true for: %s\n", proc->proc_name);

    return NULL;
}

pthread_t pumper_thread(struct process_comms* proc) {
    pthread_t proc_pumper;
    int t_suc = pthread_create(&proc_pumper, NULL, pump_output, proc);
    if (DEBUG) printf("THREAD 2: %d\n", t_suc);
    return proc_pumper;
}

void wait_pumper(pthread_t proc_thr) {
    int res = pthread_join(proc_thr, NULL);
    if (DEBUG) printf("THREAD join: %d\n", res);
}

int main(int argc, char* argv[]) {
    if (DEBUG) puts("****** START *******");

    // whether the waitpid thread should keep spinning
    __atomic_store_n(&run_waiter, true, __ATOMIC_SEQ_CST);
    pthread_mutex_init(&linked_list_mut, NULL);

    // setup for us to indicate whether we need to close process' pipes
    signal(SIGCHLD, process_closure);
    
    // thread to lookout for SIGCHLDs
    pthread_t process_waiter;
    int t_suc = pthread_create(&process_waiter, NULL, proc_waiter, NULL);
    if (DEBUG) printf("THREAD 1: %d\n", t_suc);

    char* prog1[] = {"/bin/echo", "burgers are highly regarded", NULL};
    char* prog2[] = {"/bin/grep", "-o", "gh", NULL};
    char* prog3[] = {"/bin/echo", NULL};

    struct process_comms* proc1 = make_process(prog1);
    struct process_comms* proc2 = make_process(prog2);
    struct process_comms* proc3 = make_process(prog3);
    append_active_proc(proc1);
    append_active_proc(proc2);
    append_active_proc(proc3);

    // connect echo to grep
    add_successor(proc1, proc2);
    // can even have this too, to forward output
    /*add_successor(proc1, proc2);*/
    add_successor(proc2, proc3);

    pthread_t proc1_pumper = pumper_thread(proc1);
    pthread_t proc2_pumper = pumper_thread(proc2);
    pthread_t proc3_pumper = pumper_thread(proc3);

    wait_pumper(proc1_pumper);
    wait_pumper(proc2_pumper);
    wait_pumper(proc3_pumper);

    __atomic_store_n(&run_waiter, false, __ATOMIC_SEQ_CST);
    int res = pthread_join(process_waiter, NULL);
    if (DEBUG) printf("THREAD 1 join: %d\n", res);

    // cleanup
    struct active_processes* curr = root;
    while (curr != NULL) {
        struct active_processes* tmp = curr->next;
        remove_linked_list_node(curr);
        curr = tmp;
    }

    if (DEBUG) puts("****** END *******");
}

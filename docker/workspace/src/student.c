/*
 * student.c
 * Multithreaded OS Simulation for CS 2200
 * Spring 2023
 *
 * This file contains the CPU scheduler for the simulation.
 */

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "student.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

/** Function prototypes **/
extern void idle(unsigned int cpu_id);
extern void preempt(unsigned int cpu_id);
extern void yield(unsigned int cpu_id);
extern void terminate(unsigned int cpu_id);
extern void wake_up(pcb_t *process);
int get_timeslice(void);

static unsigned int cpu_count;

/*
 * current[] is an array of pointers to the currently running processes.
 * There is one array element corresponding to each CPU in the simulation.
 *
 * current[] should be updated by schedule() each time a process is scheduled
 * on a CPU.  Since the current[] array is accessed by multiple threads, you
 * will need to use a mutex to protect it.  current_mutex has been provided
 * for your use.
 *
 * rq is a pointer to a struct you should use for your ready queue
 * implementation. The head of the queue corresponds to the process
 * that is about to be scheduled onto the CPU, and the tail is for
 * convenience in the enqueue function. See student.h for the
 * relevant function and struct declarations.
 *
 * Similar to current[], rq is accessed by multiple threads,
 * so you will need to use a mutex to protect it. current_mutex has been
 * provided for that purpose.
 *
 * The condition variable queue_not_empty has been provided for you
 * to use in conditional waits and signals.
 *
 * Please look up documentation on how to properly use pthread_mutex_t
 * and pthread_cond_t.
 *
 * A scheduler_algorithm variable and sched_algorithm_t enum have also been
 * supplied to you to keep track of your scheduler's current scheduling
 * algorithm. You should update this variable according to the program's
 * command-line arguments. Read student.h for the definitions of this type.
 */
static pcb_t **current;
static queue_t *rq;

static pthread_mutex_t current_mutex;
static pthread_mutex_t queue_mutex;
static pthread_cond_t queue_not_empty;

static sched_algorithm_t scheduler_algorithm;
static unsigned int cpu_count;
static unsigned int age_weight;
unsigned int rr_timeslice;

/** ------------------------Problem 3-----------------------------------
 * Checkout PDF Section 5 for this problem
 * 
 * priority_with_age() is a helper function to calculate the priority of a process
 * taking into consideration the age of the process.
 * 
 * It is determined by the formula:
 * Priority With Age = Priority + (Current Time - Enqueue Time) * Age Weight
 * 
 * @param current_time current time of the simulation
 * @param process process that we need to calculate the priority with age
 * 
 */
extern double priority_with_age(unsigned int current_time, pcb_t *process) {
    return (process->priority + (current_time - process->enqueue_time) * age_weight);
}

/** ------------------------Problem 0 & 3-----------------------------------
 * Checkout PDF Section 2 and 5 for this problem
 * 
 * enqueue() is a helper function to add a process to the ready queue.
 * 
 * NOTE: For Priority and FCFS scheduling, you will need to have additional logic
 * in this function and/or the dequeue function to account for enqueue time
 * and age to pick the process with the smallest age priority.
 * 
 * We have provided a function get_current_time() which is prototyped in os-sim.h.
 * Look there for more information.
 *
 * @param queue pointer to the ready queue
 * @param process process that we need to put in the ready queue
 */
void enqueue(queue_t *queue, pcb_t *process)
{
    if (queue->head != NULL) {
        queue->tail->next = process;
        queue->tail = process;
    } else {
        queue->head = process;
        queue->tail = process;
    }
    process->enqueue_time = get_current_time();
    pthread_cond_signal(&queue_not_empty);
}

/**
 * dequeue() is a helper function to remove a process to the ready queue.
 *
 * NOTE: For Priority and FCFS scheduling, you will need to have additional logic
 * in this function and/or the enqueue function to account for enqueue time
 * and age to pick the process with the smallest age priority.
 * 
 * We have provided a function get_current_time() which is prototyped in os-sim.h.
 * Look there for more information.
 * 
 * @param queue pointer to the ready queue
 */
pcb_t *dequeue(queue_t *queue)
{
    /* Handle if ready queue is already empty or if it is empty after this removal. */
    if (queue->head == NULL) {
        return NULL;
    }

    pcb_t *prev;
    pcb_t *winner = queue->head;
    pcb_t *curr;

    if (queue->head == queue->tail) {
        queue->head = NULL;
        queue->tail = NULL;
        winner->next = NULL;
        return winner;
    }

    if (scheduler_algorithm == FCFS) {
        curr = winner;
        unsigned int earliest_time = curr->arrival_time;

        while (curr != NULL) {
            if (curr->arrival_time < earliest_time) {
                earliest_time = curr->arrival_time;
                winner = curr;
            }
            curr = curr->next;
        }
        if (winner == queue->head) {
            queue->head = queue->head->next;
        } else {
            curr = queue->head->next;
            prev = queue->head;
            while (curr != NULL) {
                if (curr == winner && winner != queue->tail) {
                    prev->next = curr->next;
                    break;
                } else if (curr == winner && winner == queue->tail) {
                    queue->tail = prev;
                    prev->next = NULL;
                    break;
                }
                prev = curr;
                curr = curr->next;
            }
        }
        winner->next = NULL;
        return winner;

    } 
    // else if (scheduler_algorithm == PA) {
    //     double high_p = priority_with_age(get_current_time(), winner);
    //     curr = winner->next;
    //     prev = winner;
    //     while (curr != NULL) {
    //         unsigned int t = get_current_time();
    //         double curr_p = priority_with_age(t, curr);
    //         if (curr_p > high_p) {
    //             high_p = curr_p;
    //             winner = curr;
    //         } else {
    //             prev = curr;
    //             curr = curr->next;
    //         }
    //     }
    //     if (winner == queue->head) {
    //         queue->head = queue->head->next;
    //     } else if (winner == queue->tail) {
    //         queue->tail = prev;
    //         prev->next = NULL;
    //     } else {
    //         prev->next = curr->next;
    //     }
    //     winner->next = NULL;        
    //     return winner;
    // } else if (scheduler_algorithm == RR) {

    // }
    return winner;
}

/** ------------------------Problem 0-----------------------------------
 * Checkout PDF Section 2 for this problem
 * 
 * is_empty() is a helper function that returns whether the ready queue
 * has any processes in it.
 * 
 * @param queue pointer to the ready queue
 * 
 * @return a boolean value that indicates whether the queue is empty or not
 */
bool is_empty(queue_t *queue)
{
    bool res = false;
    pthread_mutex_lock(&queue_mutex);
    if (queue->head == NULL) {
        res = true;
    } 
    pthread_mutex_unlock(&queue_mutex);
    return res;
}

/*
 * If scheduler_algorithm is FCFS, timeslice should be set to -1;
*/
int get_timeslice() {
    if (scheduler_algorithm == FCFS) {
        return -1;
    } else {
        return (int) get_current_time();
    }
}

/** ------------------------Problem 1B-----------------------------------
 * Checkout PDF Section 3 for this problem
 * 
 * schedule() is your CPU scheduler.
 * 
 * Remember to specify the timeslice if the scheduling algorithm is Round-Robin
 * 
 * @param cpu_id the target cpu we decide to put our process in
 */
static void schedule(unsigned int cpu_id)
{
    pcb_t *pcb;
    if (is_empty(rq)) {
        pcb = NULL;
    } else {
        pthread_mutex_lock(&queue_mutex);
        pcb = dequeue(rq);
        pcb->state = PROCESS_RUNNING;
        pthread_mutex_unlock(&queue_mutex);
    }
    pthread_mutex_lock(&current_mutex);
    current[cpu_id] = pcb;
    pthread_mutex_unlock(&current_mutex);
    context_switch(cpu_id, pcb, get_timeslice());
}

/**  ------------------------Problem 1A-----------------------------------
 * Checkout PDF Section 3 for this problem
 * 
 * idle() is your idle process.  It is called by the simulator when the idle
 * process is scheduled. This function should block until a process is added
 * to your ready queue.
 *
 * @param cpu_id the cpu that is waiting for process to come in
 */
extern void idle(unsigned int cpu_id)
{
    pthread_mutex_lock(&queue_mutex);
    while (rq->head == NULL) {
        pthread_cond_wait(&queue_not_empty, &queue_mutex);
    }
    pthread_mutex_unlock(&queue_mutex);
    schedule(cpu_id);

    /*
     * REMOVE THE LINE BELOW AFTER IMPLEMENTING IDLE()
     *
     * idle() must block when the ready queue is empty, or else the CPU threads
     * will spin in a loop.  Until a ready queue is implemented, we'll put the
     * thread to sleep to keep it from consuming 100% of the CPU time.  Once
     * you implement a proper idle() function using a condition variable,
     * remove the call to mt_safe_usleep() below.
     */
    // mt_safe_usleep(1000000);
}

/** ------------------------Problem 2 & 3-----------------------------------
 * Checkout Section 4 and 5 for this problem
 * 
 * preempt() is the handler used in Round-robin and Preemptive Priority 
 * Scheduling
 *
 * This function should place the currently running process back in the
 * ready queue, and call schedule() to select a new runnable process.
 * 
 * @param cpu_id the cpu in which we want to preempt process
 */
extern void preempt(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    pcb_t *curr = current[cpu_id];
    pthread_mutex_unlock(&current_mutex);

    pthread_mutex_lock(&queue_mutex);
    enqueue(rq, curr);
    pthread_mutex_unlock(&queue_mutex);

    schedule(cpu_id);
}

/**  ------------------------Problem 1A-----------------------------------
 * Checkout PDF Section 3 for this problem
 * 
 * yield() is the handler called by the simulator when a process yields the
 * CPU to perform an I/O request.
 *
 * @param cpu_id the cpu that is yielded by the process
 */
extern void yield(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_WAITING;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}

/**  ------------------------Problem 1A-----------------------------------
 * Checkout PDF Section 3
 * 
 * terminate() is the handler called by the simulator when a process completes.
 * 
 * @param cpu_id the cpu we want to terminate
 */
extern void terminate(unsigned int cpu_id)
{
    pthread_mutex_lock(&current_mutex);
    current[cpu_id]->state = PROCESS_TERMINATED;
    pthread_mutex_unlock(&current_mutex);
    schedule(cpu_id);
}

/**  ------------------------Problem 1A & 3---------------------------------
 * Checkout PDF Section 3 and 5 for this problem
 * 
 * wake_up() is the handler called by the simulator when a process's I/O
 * request completes. This method will also need to handle priority, 
 * Look in section 5 of the PDF for more info.
 * 
 * We have provided a function get_current_time() which is prototyped in os-sim.h.
 * Look there for more information.
 * 
 * @param process the process that finishes I/O and is ready to run on CPU
 */
extern void wake_up(pcb_t *process)
{
    pthread_mutex_lock(&queue_mutex);
    enqueue(rq, process);
    process->state = PROCESS_READY;
    pthread_mutex_unlock(&queue_mutex);

    // if (scheduler_algorithm == PA) {
    //     double pwa = priority_with_age(get_current_time(), process);
    //     pthread_mutex_lock(&current_mutex);
    //     for (unsigned int i = 0; i < cpu_count; i++) {
    //         if (current[i] == NULL) {
    //             break;
    //         } else if (priority_with_age(get_current_time(), current[i]) > pwa) {
    //             pthread_mutex_unlock(&current_mutex);
    //             force_preempt(i);
    //         }
    //     }
    // }
}

/**
 * main() simply parses command line arguments, then calls start_simulator().
 * Add support for -r and -p parameters. If no argument has been supplied, 
 * you should default to FCFS.
 * 
 * HINT:
 * Use the scheduler_algorithm variable (see student.h) in your scheduler to 
 * keep track of the scheduling algorithm you're using.
 */
int main(int argc, char *argv[])
{
    /* FIX ME */
    scheduler_algorithm = FCFS;
    age_weight = 0;
    rr_timeslice = 0;

    if (argc != 2)
    {
        fprintf(stderr, "CS 2200 Project 4 -- Multithreaded OS Simulator\n"
                        "Usage: ./os-sim <# CPUs> [ -r <time slice> | -p <age weight> ]\n"
                        "    Default : FCFS Scheduler\n"
                        "         -r : Round-Robin Scheduler\n1\n"
                        "         -p : Priority Aging Scheduler\n");
        return -1;
    }


    /* Parse the command line arguments */
    cpu_count = strtoul(argv[1], NULL, 0);

    /* Allocate the current[] array and its mutex */
    current = malloc(sizeof(pcb_t *) * cpu_count);
    assert(current != NULL);
    pthread_mutex_init(&current_mutex, NULL);
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_not_empty, NULL);
    rq = (queue_t *)malloc(sizeof(queue_t));
    assert(rq != NULL);

    /* Start the simulator in the library */
    start_simulator(cpu_count);

    return 0;
}

#pragma GCC diagnostic pop

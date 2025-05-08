
#include "scheduler.h"
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <stdio.h>    
#include <errno.h>    
#include <math.h>

#define MSG_KEY 1234
#define MAX_PROCESSES 100
#define MIN(a,b) ((a) < (b) ? (a) : (b))
int quantum = 2; // Example quantum, can be set dynamically

CircularQueue * readyQueue=NULL;
PC* currentProcess = NULL;

PC completed[MAX_PROCESSES];
int completedCount = 0;
float totalWTA = 0;
int totalTA = 0, totalRT = 0, totalRuntime=0;
float avgWTA, avgTA, avgRT,stdDevWTA,CPU_util;

int queueSize = 0;

int idleCycleCount, sumWaitingTime;
float sumWTA;
int runningProcessesStartTime;
int runningProcessesEndTime;
int static totalProcesses = 0;
static int finished_count   = 0;

FILE *processesFile;
FILE *logFile;

// Circular Queue Functions
CircularQueue* createQueue(int capacity) {
    CircularQueue* q = (CircularQueue*)malloc(sizeof(CircularQueue));
    if (!q) {
        perror("Failed to allocate memory for queue structure");
        return NULL;
    }
    // Allocate memory for the array of Process pointers
    q->data = (PC**)malloc(capacity * sizeof(PC*));
    if (!q->data) {
        perror("Failed to allocate memory for queue array");
        free(q); 
        return NULL;
    }
    q->size = capacity;
    q->front = 0;
    q->rear = -1;
    q->count = 0;
    return q;
}

void destroyQueue(CircularQueue* q) {
    if (q) {
        free(q->data); // Free the array of pointers
        free(q);       // Free the queue structure
    }
}


bool isFull(CircularQueue* q) {
    if (!q) return false; 
    return (q->count == q->size);
}


bool isEmpty(CircularQueue* q) {
    if (!q) return true; 
   return (q->count == 0);
}


bool enqueue(CircularQueue* q, PC* p) {
   if (!q || !p) {
        fprintf(stderr, "Error: Cannot enqueue to NULL queue or NULL process.\n");
        return false;
   }
   if (isFull(q)) {
       fprintf(stderr, "Queue Overflow: Cannot enqueue PID %d\n", p->pid);
       return false; 
   }
   // Calculate the new rear position using modulo arithmetic for circularity
   q->rear = (q->rear + 1) % q->size;
   q->data[q->rear] = p;
   q->count++;
   return true; 
}


PC* dequeue(CircularQueue *q) {
    PC* p = NULL;
    if (isEmpty(q)) return p;
    p = q->data[q->front];
    q->front = (q->front + 1) % q->size;
    q->count--;
    return p;
}

PC* peek(CircularQueue* q) {
    if (!q || isEmpty(q)) return NULL;
    return q->data[q->front];
}


void initScheduler()
{
    logFile = fopen("scheduler.log", "w");
    if (logFile == NULL)
    {
        printf("Failed to open log file\n");
        exit(1);
    }
    
    processesFile = fopen("processes.txt", "r");
    if (processesFile == NULL)
    {
        printf("Failed to open processes file\n");
        exit(1);
    }
    
}
// end 

// MinHeap Functions
void initMinHeap(MinHeap *heap) {
    heap->Heapsize = 0;
}

// Function to check if the Min Heap is full
int HeapisFull(MinHeap *heap) {
    return heap->Heapsize == MAX_PROCESSES;
}

// Function to check if the Min Heap is empty
int HeapisEmpty(MinHeap *heap) {
    return heap->Heapsize == 0;
}

// Function to swap two processes
void swap(PC *a, PC *b) {
    PC temp = *a;
    *a = *b;
    *b = temp;
}

// Function to heapify the Min Heap at a given index
void heapify(MinHeap *heap, int index) {
    int left = 2 * index + 1;
    int right = 2 * index + 2;
    int smallest = index;

    // Find the smallest among the current node, left child, and right child
    if (left < heap->Heapsize && heap->data[left].remainingTime < heap->data[smallest].remainingTime)
        smallest = left;

    if (right < heap->Heapsize && heap->data[right].remainingTime < heap->data[smallest].remainingTime)
        smallest = right;

    // Swap and heapify if needed
    if (smallest != index) {
        swap(&heap->data[index], &heap->data[smallest]);
        heapify(heap, smallest);
    }
}
void heapify_HPF(MinHeap *heap, int index) {
    int left = 2 * index + 1;
    int right = 2 * index + 2;
    int smallest = index;

    // Compare left child
    if (left < heap->Heapsize) {
        if (heap->data[left].priority < heap->data[smallest].priority ||
            (heap->data[left].priority == heap->data[smallest].priority &&
             heap->data[left].arrivalTime < heap->data[smallest].arrivalTime)) {
            smallest = left;
        }
    }

    // Compare right child
    if (right < heap->Heapsize) {
        if (heap->data[right].priority < heap->data[smallest].priority ||
            (heap->data[right].priority == heap->data[smallest].priority &&
             heap->data[right].arrivalTime < heap->data[smallest].arrivalTime)) {
            smallest = right;
        }
    }

    // Swap and recurse if needed
    if (smallest != index) {
        swap(&heap->data[index], &heap->data[smallest]);
        heapify_HPF(heap, smallest);
    }
}


// Function to insert a process into the Min Heap
void insert(MinHeap *heap, PC* p) {
    if (HeapisFull(heap)) {
        printf("Min Heap is full, cannot insert process\n");
        return;
    }

    // Insert the new process at the end of the heap
    heap->data[heap->Heapsize] = *p;
    heap->Heapsize++;

    // Fix the heap property by bubbling up
    int index = heap->Heapsize - 1;
    while (index > 0 && heap->data[(index - 1) / 2].remainingTime > heap->data[index].remainingTime) {
        swap(&heap->data[index], &heap->data[(index - 1) / 2]);
        index = (index - 1) / 2;
    }
}
void insert_HPF(MinHeap *heap, PC* p) {
    if (HeapisFull(heap)) {
        printf("Min Heap is full, cannot insert process\n");
        return;
    }

    // Insert the new process at the end of the heap
    heap->data[heap->Heapsize] = *p;
    heap->Heapsize++;

    // Fix the heap property by bubbling up
    int index = heap->Heapsize - 1;
    while (index > 0) {
        int parent = (index - 1) / 2;

        // Check if current has higher priority OR same priority but earlier arrival
        if (heap->data[index].priority < heap->data[parent].priority ||
            (heap->data[index].priority == heap->data[parent].priority &&
             heap->data[index].arrivalTime < heap->data[parent].arrivalTime)) {
            swap(&heap->data[index], &heap->data[parent]);
            index = parent;
        } else {
            break;
        }
    }
}


// Function to extract the process with the shortest remaining time
PC extractMin(MinHeap *heap) {
    if (HeapisEmpty(heap)) {
        printf("Min Heap is empty, cannot extract process\n");
        PC emptyProcess = {-1, -1}; // Return an empty process in case of error
        return emptyProcess;
    }

    // The root contains the minimum element
    PC minProcess = heap->data[0];

    // Replace the root with the last element and decrease the size
    heap->data[0] = heap->data[heap->Heapsize - 1];
    heap->Heapsize--;

    // Restore the heap property by heapifying
    heapify(heap, 0);

    return minProcess;
}
PC extractMin_HPF(MinHeap *heap) {
    if (HeapisEmpty(heap)) {
        printf("Min Heap is empty, cannot extract process\n");
        PC emptyProcess = {-1, -1}; // Return an empty process in case of error
        return emptyProcess;
    }

    // The root contains the minimum element
    PC minProcess = heap->data[0];

    // Replace the root with the last element and decrease the size
    heap->data[0] = heap->data[heap->Heapsize - 1];
    heap->Heapsize--;

    // Restore the heap property by heapifying
    heapify_HPF(heap, 0);

    return minProcess;
}

// end 

void pollArrivals(CircularQueue *q) {
    ProcMsg pm;
    ssize_t r;
    int msqid = msgget(MSG_KEY, 0);
    while ((r = msgrcv(msqid, &pm, sizeof(pm.process), 2, IPC_NOWAIT)) > 0) {
        PC *p = malloc(sizeof(PC));
        *p = pm.process;
        p->remainingTime = p->runningTime;
        p->state         = NEW;
        p->startTime     = -1;
        p->finishTime    = -1;
        enqueue(q, p);
        updateProcess(READY, p);
    }
    if (r < 0 && errno != ENOMSG) {
        perror("msgrcv(RR)");
        exit(1);
    }
}

void pollArrivalsForMinHeap(MinHeap *heap) {
    ProcMsg pm;
    ssize_t r;
    int msqid = msgget(MSG_KEY, 0);
    
    while ((r = msgrcv(msqid, &pm, sizeof(pm.process), 2, IPC_NOWAIT)) > 0) {
        // Allocate memory for the new process and initialize it
        PC *p = malloc(sizeof(PC));
        *p = pm.process;
        p->remainingTime = p->runningTime;  // Initialize remainingTime
        p->state         = NEW;             // Set state to NEW
        p->startTime     = -1;              // Not started yet
        p->finishTime    = -1;              // Not finished yet

        // Insert the process into the MinHeap based on remainingTime
        insert(heap, p); // Ensure the heap maintains the min-heap property on remainingTime

        // Update the process state to READY
        updateProcess(READY, p);
    }

    // Handle any potential errors from msgrcv
    if (r < 0 && errno != ENOMSG) {
        perror("msgrcv(MinHeap)");
        exit(1);
    }
}

void pollArrivalsForMinHeap_HPF(MinHeap *heap) {
    ProcMsg pm;
    ssize_t r;
    int msqid = msgget(MSG_KEY, 0);
    
    while ((r = msgrcv(msqid, &pm, sizeof(pm.process), 2, IPC_NOWAIT)) > 0) {
        // Allocate memory for the new process and initialize it
        PC *p = malloc(sizeof(PC));
        *p = pm.process;
        p->remainingTime = p->runningTime;  // Initialize remainingTime
        p->state         = NEW;             // Set state to NEW
        p->startTime     = -1;              // Not started yet
        p->finishTime    = -1;              // Not finished yet

        // Insert the process into the MinHeap based on proirity
        insert_HPF(heap, p);

        // Update the process state to READY
        updateProcess(READY, p);
    }

    // Handle any potential errors from msgrcv
    if (r < 0 && errno != ENOMSG) {
        perror("msgrcv(MinHeap)");
        exit(1);
    }
}

void logProcessLine(int time, int pid, const char *event,PC *p,int wait,int ta,float wta)
{
    if (ta >= 0) {
        // termination line with TA & WTA
        fprintf(logFile,
        "At time %d process %d %s arr %d total %d remain %d wait %d TA %d WTA %.2f\n",
        time, pid, event,
        p->arrivalTime,
        p->runningTime,
        p->remainingTime,
        wait,
        ta,
        wta
        );
    } else {
        // non-termination line, no TA/WTA
        fprintf(logFile,
        "At time %d process %d %s arr %d total %d remain %d wait %d\n",
        time, pid, event,
        p->arrivalTime,
        p->runningTime,
        p->remainingTime,
        wait
        );
    }
    fflush(logFile);
}

void updateProcess(processState state, PC *p) {
        int t = getClk();
        int total    = p->runningTime;
        int rem      = p->remainingTime;
        int waited   = t - p->arrivalTime - (total - rem);
        int ta       = -1;
        float wta    = 0.0f;

        switch (state) {
        case READY:
        // preemption
        logProcessLine(t, p->id, "stopped", p, waited, -1, 0.0f);
        break;

        case RUNNING:
        if (p->startTime < 0) {
        // first time ever running
        p->startTime    = t;
        p->responseTime = t - p->arrivalTime;
        logProcessLine(t, p->id, "started", p, waited, -1, 0.0f);
        } else {
        // a resume after preemption
        logProcessLine(t, p->id, "resumed", p, waited, -1, 0.0f);
        }
        break;

        case TERMINATED:
        // compute final metrics
        p->finishTime = t;
        ta    = p->finishTime - p->arrivalTime;
        wta   = (float)ta / total;
        logProcessLine(t, p->id, "finished", p, waited, ta, wta);
        break;
        }

        p->state = state;
}


void calculator(PC completed[], int count) {
    
    for (int i = 0; i < count; i++) {
        totalWTA     += completed[i].weightedTurnaroundTime;
        totalTA      += completed[i].turnaroundTime;
        totalRT      += completed[i].responseTime;
        totalRuntime += completed[i].runningTime;
    }
    avgWTA = (float)totalWTA / count;
    avgTA  = (float)totalTA  / count;
    avgRT  = (float)totalRT  / count;

    float devsum = 0;
    for (int i = 0; i < count; i++) {
        float diff = completed[i].weightedTurnaroundTime - avgWTA;
        devsum += diff * diff;
    }
    stdDevWTA = sqrt(devsum / count);

    CPU_util = ((float)totalRuntime / getClk()) * 100;

    // --- open the perf file ---
    FILE *fp = fopen("scheduler.perf", "w");
    if (!fp) {
        perror("fopen scheduler.perf");
        return;
    }

    // --- write the same metrics to it ---
    fprintf(fp, "=== Scheduler Summary ===\n");
    fprintf(fp, "Average Turnaround Time                   = %.2f\n", avgTA);
    fprintf(fp, "Average Response Time                     = %.2f\n", avgRT);
    fprintf(fp, "Average Weighted Turnaround Time          = %.2f\n", avgWTA);
    fprintf(fp, "Standard Deviation of Weighted Turnaround = %.2f\n", stdDevWTA);
    fprintf(fp, "CPU Utilization (%%)                       = %.2f\n", CPU_util);

    fclose(fp);
}




// RR Scheduler
void scheduleRoundRobin(int totalProcesses, int quantum) {
    CircularQueue *q = createQueue(totalProcesses);
    int finished = 0;
    int now =getClk();

    while (finished < totalProcesses) {
        pollArrivals(q);
        
        if (!isEmpty(q)) {
            PC *curr = dequeue(q);
            //updateProcess(READY, curr);
            

            if (!curr->pid) {
                curr->pid = fork();
                if (curr->pid == 0) {
                    // pass the *remainingTime* as the child’s argv[1]
                    char rt_str[16];
                    snprintf(rt_str, sizeof(rt_str), "%d", curr->remainingTime);
                    execlp("./process.out", "process.out", rt_str, NULL);
                } now=getClk();
            } else {
                kill(curr->pid, SIGCONT);
                updateProcess (RUNNING, curr);
            }

            
            int start = getClk();
            if (curr->startTime < 0) 
                curr->startTime = start;
            updateProcess(RUNNING, curr);

            int slice = MIN(curr->remainingTime, quantum);
            int end   = start + slice;
             while (getClk() < end) {
                pollArrivals(q);
                //sleep(1);
                usleep(100000);
             }

            kill(curr->pid, SIGSTOP);
            curr->remainingTime -= slice;
            now=getClk();

            if (curr->remainingTime > 0) {
                updateProcess(READY, curr);
                enqueue(q, curr);
            } else {
                curr->finishTime = end;
                // kill(curr->pid, SIGCONT);
                // waitpid(curr->pid, NULL, 0);
                //curr->finishTime = getClk();
                curr->turnaroundTime = curr->finishTime - curr->arrivalTime;
                curr->responseTime = curr->startTime - curr->arrivalTime;
                curr->weightedTurnaroundTime=(float)curr->turnaroundTime / curr->runningTime;
                updateProcess(TERMINATED, curr);
                completed[completedCount++] = *curr;
                finished++;
                free(curr);
            }
        }
        else {
            now=getClk();
            sleep(1);
            pollArrivals(q); // Check for new arrivals
            idleCycleCount++;
        }
    }

    destroyQueue(q);
}



//SRTN Scheduler

void scheduleSRTN(int totalProcesses) {
    MinHeap *heap = malloc(sizeof(MinHeap));
    initMinHeap(heap);
    //PC receivedProcess;
    int finished = 0;
    PC *curr = NULL;

    while (finished < totalProcesses) {
        pollArrivalsForMinHeap(heap);


        // If there's a process running and a new one has shorter remaining time, preempt
        if (curr && !HeapisEmpty(heap) && heap->data[0].remainingTime < curr->remainingTime) {
            kill(curr->pid, SIGSTOP);
            updateProcess(READY, curr);
            insert(heap, curr);
            free(curr);
            curr = NULL;
        }

        // If no process is running, start the next shortest one
        if (!curr && !HeapisEmpty(heap)) {
            curr = malloc(sizeof(PC));
            *curr = extractMin(heap);

            if (!curr->pid) {
                curr->pid = fork();
                if (curr->pid == 0) {
                    char rt_str[16];
                    snprintf(rt_str, sizeof(rt_str), "%d", curr->remainingTime);
                    execlp("./process.out", "process.out", rt_str, NULL);
                }
            } else {
                kill(curr->pid, SIGCONT);
            }

            if (curr->startTime < 0)
                curr->startTime = getClk();

            updateProcess(RUNNING, curr);
        }

        // Let the current process run for 1 time unit
        if (curr) {
            sleep(1);
            curr->remainingTime--;

            // If finished, terminate it
            if (curr->remainingTime <= 0) {
                kill(curr->pid, SIGSTOP);
                curr->finishTime = getClk();
                curr->turnaroundTime = curr->finishTime - curr->arrivalTime;
                curr->responseTime = curr->startTime - curr->arrivalTime;
                curr->weightedTurnaroundTime=(float)curr->turnaroundTime / curr->runningTime;
                updateProcess(TERMINATED, curr);
                completed[completedCount++] = *curr;
                free(curr);
                curr = NULL;
                finished++;
                printf("Finished process %d at time%d\n", finished, getClk());
            }
        } else {
            sleep(1);  // idle cycle
        }
    }

    //destroyMinHeap(heap);
}

void scheduleHPF(int totalProcesses) {
    MinHeap *heap = malloc(sizeof(MinHeap));
    initMinHeap(heap);
    int finished = 0;
    PC *curr = NULL;

    while (finished < totalProcesses) {
        pollArrivalsForMinHeap_HPF(heap);  

        // If no process is running, pick the highest priority one (lowest value)
        if (!curr && !HeapisEmpty(heap)) {
            curr = malloc(sizeof(PC));
            *curr = extractMin_HPF(heap);  // top of the heap = highest priority

            curr->pid = fork();
            if (curr->pid == 0) {
                // child runs the process
                char rt_str[16];
                snprintf(rt_str, sizeof(rt_str), "%d", curr->remainingTime);
                execlp("./process.out", "process.out", rt_str, NULL);
            }

            curr->startTime = getClk();
            updateProcess(RUNNING, curr);

            // Wait for process to finish
            int status;
            waitpid(curr->pid, &status, 0);

            curr->finishTime = getClk();
            curr->turnaroundTime = curr->finishTime - curr->arrivalTime;
            curr->responseTime = curr->startTime - curr->arrivalTime;
            curr->weightedTurnaroundTime=(float)curr->turnaroundTime / curr->runningTime;
            updateProcess(TERMINATED, curr);
            completed[completedCount++] = *curr;
            printf("Finished process %d at time %d\n", curr->id, curr->finishTime);
            free(curr);
            curr = NULL;
            finished++;
        } else {
            sleep(1);  // Idle wait
        }
    }
}

static void reap_children(int signo)
{
    (void)signo;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        
    }
}



int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <HPF|SRTN|RR> [quantum]\n", argv[0]);
        exit(1);
    }

    // <<< CHANGED: get algorithm + quantum from argv
    char *algo    = argv[1];
    int   quantum = (argc>=3 && strcmp(algo,"RR")==0)? atoi(argv[2]): 0;

    initClk();           // start clock
    initScheduler();     // open log, etc.
    printf(">> Debug: scheduling %s with quantum=%d\n", algo, quantum);


    int msqid = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msqid < 0) { perror("msgget"); exit(1); }

    // <<< CHANGED: receive only the count
    CountMsg cm;
    if (msgrcv(msqid, &cm, sizeof(cm.count), 1, 0) == -1) {
        perror("msgrcv(count)"); exit(1);
    }
    int totalProcesses = cm.count;
    

    // dispatch based on argv
    if (strcmp(algo, "RR") == 0) {
        scheduleRoundRobin(totalProcesses, quantum);   // <<< CHANGED
    }
     else if (strcmp(algo, "SRTN") == 0) {
         scheduleSRTN(totalProcesses);
     }
    else {  // HPF
        scheduleHPF(totalProcesses);
    }
    calculator(completed, completedCount);
    destroyClk(true);
    return 0;
}




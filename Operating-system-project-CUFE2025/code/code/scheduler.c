
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
#include "buddy.c"
#define MSG_KEY 1234
#define MAX_PROCESSES 100
#define MIN(a,b) ((a) < (b) ? (a) : (b))
int quantum = 2; // Example quantum, can be set dynamically

CircularQueue * readyQueue=NULL;
CircularQueue * BlockedQueue=NULL;
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
//CircularQueue* Waitingqueue; // to store processes when no memory available
FILE *processesFile;
FILE *logFile;
FILE *memoryLogFile;

CircularQueue *arrivalQueue = NULL;

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

    memoryLogFile = fopen("memory.log", "w");
    if (!memoryLogFile) {
    perror("Failed to open memory.log");
    exit(1);
}

    arrivalQueue = createQueue(MAX_PROCESSES);  // Initialize arrival queue
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

    // Check if left child is "smaller" (based on remainingTime then arrivalTime)
    if (left < heap->Heapsize) {
        if (heap->data[left].remainingTime < heap->data[smallest].remainingTime ||
            (heap->data[left].remainingTime == heap->data[smallest].remainingTime &&
             heap->data[left].arrivalTime < heap->data[smallest].arrivalTime)) {
            smallest = left;
        }
    }

    // Check if right child is "smaller"
    if (right < heap->Heapsize) {
        if (heap->data[right].remainingTime < heap->data[smallest].remainingTime ||
            (heap->data[right].remainingTime == heap->data[smallest].remainingTime &&
             heap->data[right].arrivalTime < heap->data[smallest].arrivalTime)) {
            smallest = right;
        }
    }
    // Swap and continue heapifying if necessary
    if (smallest != index) {
        swap(&heap->data[index], &heap->data[smallest]);
        heapify(heap, smallest);
    }
}

////////////
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
    while (index > 0) {
        int parent = (index - 1) / 2;

        // Check if current has higher priority OR same priority but earlier arrival
        if (heap->data[index].remainingTime < heap->data[parent].remainingTime ||
            (heap->data[index].remainingTime == heap->data[parent].remainingTime &&
             heap->data[index].arrivalTime < heap->data[parent].arrivalTime)) {
            swap(&heap->data[index], &heap->data[parent]);
            index = parent;
        } else {
            break;
        }
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
    int current_time = getClk();
    
    // First, check if any processes in arrival queue should be moved to ready queue
    while (!isEmpty(arrivalQueue)) {
        PC *p = peek(arrivalQueue);
        if (current_time >= p->arrivalTime) {
            p = dequeue(arrivalQueue);
            enqueue(q, p);
            updateProcess(READY, p);
        } else {
            break;  // No more processes ready to arrive
        }
    }
    
    // Then handle new messages
    while ((r = msgrcv(msqid, &pm, sizeof(pm.process), 2, IPC_NOWAIT)) > 0) {
        PC *p = malloc(sizeof(PC));
        *p = pm.process;
        p->remainingTime = p->runningTime;
        p->state = NEW;
        p->startTime = -1;
        p->finishTime = -1;
        
        if (current_time >= p->arrivalTime) {
            enqueue(q, p);
            updateProcess(READY, p);
        } else {
            enqueue(arrivalQueue, p);  // Store in arrival queue
        }
    }
    if (r < 0 && errno != ENOMSG) {
        perror("msgrcv(RR)");
        exit(1);
    }
}

void pollArrivalsForMinHeap(MinHeap *heap, CircularQueue* Waitingqueue) {
    ProcMsg pm;
    ssize_t r;
    int msqid = msgget(MSG_KEY, 0);
    printf("Attempting to receive message at time %d\n", getClk());

    printf("Waiting queue has count  %d\n", Waitingqueue->count);

    while (!isEmpty(Waitingqueue)) {

        PC *blocked = peek(Waitingqueue);  // Look at the first blocked process
        void* mem_start = allocate_memory(blocked->memSize);
        
        if (mem_start != NULL) {
            // Memory is now available, dequeue from blocked and add to ready
            blocked = dequeue(Waitingqueue);
            blocked->memPtr = mem_start;
            blocked->memStart = (int)((char*)mem_start - memory);
            blocked->realBlock = get_block_size(blocked->memSize);
            
            fprintf(memoryLogFile, 
                "At time %d allocated %d bytes for process %d from %d to %d\n", 
                getClk(), 
                blocked->memSize, 
                blocked->id, 
                blocked->memStart, 
                blocked->memStart + blocked->realBlock - 1);
            fflush(memoryLogFile);
            
            updateProcess(READY, blocked);
            insert(heap, blocked);  // Add to ready queue
        } else {
            // Still no memory available, keep process blocked
            break;
        }
    }
    while ((r = msgrcv(msqid, &pm, sizeof(pm.process), 2, IPC_NOWAIT)) > 0) {
        printf("Received message for process %d at time %d\n", pm.process.id, getClk());
        // Allocate memory for the new process and initialize it
        PC *p = malloc(sizeof(PC));
        *p = pm.process;
        p->remainingTime = p->runningTime;  // Initialize remainingTime
        p->state         = NEW;             // Set state to NEW
        p->startTime     = -1;              // Not started yet
        p->finishTime    = -1;              // Not finished yet

/////////////////////////////////////////////////////////
// Allocate memory using buddy system

    void* mem_start = allocate_memory(p->memSize);
    if (mem_start == NULL) {
        // printf("Error: Not enough memory for process %d\n", p->id);
        // free(p);
                    printf("Insufficient memory for process %d, adding to waiting queue\n", p->id);
        enqueue(Waitingqueue, p);
        continue;
    }
    printf("Trying to allocate %d bytes for process %d at %p\n", p->memSize, p->id, mem_start);
    // Save memory info in the process struct
    p->memPtr = mem_start;
    p->memStart = (int)((char*)mem_start - memory); 

    fprintf(memoryLogFile, 
    "At time %d allocated %d bytes for process %d from %d to %d\n", 
    getClk(), 
    p->memSize, 
    p->id, 
    p->memStart, 
    p->memStart + p->memSize - 1);
    fflush(memoryLogFile);

    /////////////////////////////////////////////////////////
    // Insert the process into the MinHeap based on remainingTime
    insert(heap, p); // Ensure the heap maintains the min-heap property on remainingTime

    // Update the process state to READY
    updateProcess(READY, p);
    printf("received process st time %d", getClk());
    }

    // Handle any potential errors from msgrcv
    if (r < 0 && errno != ENOMSG) {
        perror("msgrcv(MinHeap)");
        exit(1);
    }
}

void pollArrivalsForMinHeap_HPF(MinHeap *heap, CircularQueue* Waitingqueue) {
    ProcMsg pm;
    ssize_t r;
    int msqid = msgget(MSG_KEY, 0);
    int current_time = getClk();

    // First, try to allocate memory for any blocked processes
    while (!isEmpty(Waitingqueue)) {
        PC *blocked = peek(Waitingqueue);
        void* mem_start = allocate_memory(blocked->memSize);

        if (mem_start != NULL) {
            blocked = dequeue(Waitingqueue);
            blocked->memPtr = mem_start;
            blocked->memStart = (int)((char*)mem_start - memory);
            blocked->realBlock = get_block_size(blocked->memSize);

            fprintf(memoryLogFile,
                "At time %d allocated %d bytes for process %d from %d to %d\n",
                current_time,
                blocked->memSize,
                blocked->id,
                blocked->memStart,
                blocked->memStart + blocked->realBlock - 1);
            fflush(memoryLogFile);

            insert_HPF(heap, blocked);
            updateProcess(READY, blocked);
        } else {
            break;
        }
    }

    // Now handle new messages
    while ((r = msgrcv(msqid, &pm, sizeof(pm.process), 2, IPC_NOWAIT)) > 0) {
        PC *p = malloc(sizeof(PC));
        *p = pm.process;
        p->remainingTime = p->runningTime;
        p->state = NEW;
        p->startTime = -1;
        p->finishTime = -1;

     

        void* mem_start = allocate_memory(p->memSize);
        if (mem_start == NULL) {
            printf("Insufficient memory for process %d, adding to waiting queue\n", p->id);
            enqueue(Waitingqueue, p);
            continue;
        }

        // Memory allocated, finalize setup
        p->memPtr = mem_start;
        p->memStart = (int)((char*)mem_start - memory);
        p->realBlock = get_block_size(p->memSize);

        fprintf(memoryLogFile,
            "At time %d allocated %d bytes for process %d from %d to %d\n",
            current_time,
            p->memSize,
            p->id,
            p->memStart,
            p->memStart + p->realBlock - 1);
        fflush(memoryLogFile);

        insert_HPF(heap, p);
        updateProcess(READY, p);
    }

    if (r < 0 && errno != ENOMSG) {
        perror("msgrcv(MinHeap_HPF)");
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

        if (p->state == RUNNING) {
                // Only log as stopped if it was previously running (preemption)
                logProcessLine(t, p->id, "stopped", p, waited, -1, 0.0f);
            } else if (p->state == NEW) {
                // Log as arrived if it's a new process
                logProcessLine(t, p->id, "arrived", p, waited, -1, 0.0f);
            }
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

        case BLOCKED:
        logProcessLine(t, p->id, "blocked", p, waited, -1, 0.0f);
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
    BlockedQueue = createQueue(totalProcesses);  // Initialize blocked queue
    int finished = 0;
    int now = getClk();
    init_buddy_system();  // Initialize buddy system

    while (finished < totalProcesses) {
        pollArrivals(q);
        
        // First, try to allocate memory for blocked processes
        while (!isEmpty(BlockedQueue)) {
            PC *blocked = peek(BlockedQueue);  // Look at the first blocked process
            void* mem_start = allocate_memory(blocked->memSize);
            
            if (mem_start != NULL) {
                // Memory is now available, dequeue from blocked and add to ready
                blocked = dequeue(BlockedQueue);
                blocked->memPtr = mem_start;
                blocked->memStart = (int)((char*)mem_start - memory);
                blocked->realBlock = get_block_size(blocked->memSize);
                
                fprintf(memoryLogFile, 
                    "At time %d allocated %d bytes for process %d from %d to %d\n", 
                    getClk(), 
                    blocked->memSize, 
                    blocked->id, 
                    blocked->memStart, 
                    blocked->memStart + blocked->realBlock - 1);
                fflush(memoryLogFile);
                
                enqueue(q, blocked);  // Add to ready queue
            } else {
                // Still no memory available, keep process blocked
                break;
            }
        }
        
        if (!isEmpty(q)) {
            PC *curr = dequeue(q);
            //updateProcess(READY, curr);
            
            // Allocate memory for the process if it hasn't been allocated yet
            if (curr->memPtr == NULL) {
                void* mem_start = allocate_memory(curr->memSize);
                if (mem_start == NULL) {
                    printf("Error: Not enough memory for process added to blocked queue %d\n", curr->id);
                    updateProcess(BLOCKED, curr);
                    enqueue(BlockedQueue, curr);
                    //free(curr);
                    continue;
                }
                curr->memPtr = mem_start;
                curr->memStart = (int)((char*)mem_start - memory);
                curr->realBlock = get_block_size(curr->memSize);
                
                fprintf(memoryLogFile, 
                    "At time %d allocated %d bytes for process %d from %d to %d\n", 
                    getClk(), 
                    curr->memSize, 
                    curr->id, 
                    curr->memStart, 
                    curr->memStart + curr->realBlock - 1);
                fflush(memoryLogFile);
            }

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
                curr->weightedTurnaroundTime = (float)curr->turnaroundTime / curr->runningTime;
                
                // Free the process's memory before terminating
                if (curr->memPtr != NULL) {
                    free_memory(curr->memPtr);
                    fprintf(memoryLogFile, 
                    "At time %d freed %d bytes for process %d from %d to %d\n", 
                    getClk(), 
                    curr->memSize, 
                    curr->id, 
                    curr->memStart, 
                    curr->memStart + curr->realBlock - 1);
                    fflush(memoryLogFile);
                    
                    curr->memPtr = NULL;
                }
                
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

    cleanup_buddy_system();  // Clean up buddy system
    destroyQueue(q);
    destroyQueue(BlockedQueue);
    destroyQueue(arrivalQueue);  // Clean up arrival queue
}



//SRTN Scheduler
void scheduleSRTN(int totalProcesses) {
    MinHeap *heap = malloc(sizeof(MinHeap));
    initMinHeap(heap);
    //PC receivedProcess;
    CircularQueue* Waitingqueue = createQueue(totalProcesses);
    int finished = 0;
    PC *curr = NULL;
    init_buddy_system();  // Buddy system should already be initialized

    while (finished < totalProcesses) {
        pollArrivalsForMinHeap(heap, Waitingqueue);

    printf("///////////////////////////////// The Time Now Is %d ////////////////////////////////\n", getClk());
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
           // sleep(1);
           int lastClk = getClk();
        while (getClk() == lastClk) usleep(10000);  // busy wait until clock advances

            curr->remainingTime--;

            // If finished, terminate it
            if (curr->remainingTime <= 0) {
                kill(curr->pid, SIGSTOP);
                curr->finishTime = getClk();
                curr->turnaroundTime = curr->finishTime - curr->arrivalTime;
                curr->responseTime = curr->startTime - curr->arrivalTime;
                curr->weightedTurnaroundTime=(float)curr->turnaroundTime / curr->runningTime;
                free_memory(curr->memPtr);  // Free memory previously allocated in pollArrivals
                fprintf(memoryLogFile,
                "At time %d freed %d bytes from process %d from %d to %d\n",
                getClk(),
                curr->memSize,
                curr->id,
                curr->memStart,
                curr->memStart + curr->memSize - 1);
                fflush(memoryLogFile);
                updateProcess(TERMINATED, curr);
                completed[completedCount++] = *curr;
                free(curr);
                curr = NULL;
                finished++;
                printf("Finished process %d at time%d\n", finished, getClk());
            }
        } 
        else {
              sleep(1);  // idle cycle
        }
    }
    cleanup_buddy_system();  // Free all memory allocated by the buddy system
    destroyQueue(Waitingqueue);
    free(heap);
}



void scheduleHPF(int totalProcesses) {
    MinHeap *heap = malloc(sizeof(MinHeap));
    initMinHeap(heap);
    CircularQueue* Waitingqueue = createQueue(totalProcesses);
    int finished = 0;
    PC *curr = NULL;
    
    init_buddy_system();  // Initialize the buddy memory system

    while (finished < totalProcesses) {
        pollArrivalsForMinHeap_HPF(heap, Waitingqueue);  // Handles arrivals + memory


        // If no process is running, start highest priority process
        if (!curr && !HeapisEmpty(heap)) {
            curr = malloc(sizeof(PC));
            *curr = extractMin_HPF(heap);

            curr->pid = fork();
            if (curr->pid == 0) {
                char rt_str[16];
                snprintf(rt_str, sizeof(rt_str), "%d", curr->remainingTime);
                execlp("./process.out", "process.out", rt_str, NULL);
            }

            if (curr->startTime < 0)
                curr->startTime = getClk();

            updateProcess(RUNNING, curr);

            // Wait for it to finish
            int status;
            int start=getClk();
           // waitpid(curr->pid, &status, 0);
            int slice = curr->remainingTime;
            int end   = start + slice;
             while (getClk() < end) {
  pollArrivalsForMinHeap_HPF(heap, Waitingqueue);                 //sleep(1);
                usleep(100000);
             }
            curr->finishTime = getClk();
            curr->turnaroundTime = curr->finishTime - curr->arrivalTime;
            curr->responseTime = curr->startTime - curr->arrivalTime;
            curr->weightedTurnaroundTime = (float)curr->turnaroundTime / curr->runningTime;

            free_memory(curr->memPtr);  // Free allocated memory
            fprintf(memoryLogFile,
                    "At time %d freed %d bytes from process %d from %d to %d\n",
                    getClk(),
                    curr->memSize,
                    curr->id,
                    curr->memStart,
                    curr->memStart + curr->realBlock - 1);
            fflush(memoryLogFile);

            updateProcess(TERMINATED, curr);
            completed[completedCount++] = *curr;
            printf("Finished process %d at time %d\n", curr->id, curr->finishTime);
            free(curr);
            curr = NULL;
            finished++;
        } else {
            sleep(1);  // idle cycle
        }
    }

    cleanup_buddy_system();  // Free all memory managed by buddy system
    destroyQueue(Waitingqueue);
    free(heap);
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
    printf("[Scheduler] Clock now = %d\n", getClk());
    // printf("intiallizing clock with time %d\n", getClk());
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
        printf("starting with time %d with totalprocesses %d\n", getClk(), totalProcesses);
         scheduleSRTN(totalProcesses);
     }
    else {  // HPF
        scheduleHPF(totalProcesses);
    }
    calculator(completed, completedCount);
    destroyClk(true);
    fclose(memoryLogFile);

    return 0;
}




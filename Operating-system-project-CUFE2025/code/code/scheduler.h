#ifndef SCHEDULER_H
#define SCHEDULER_H
#include "headers.h"

#define MAX_PROCESSES 100

//process state
typedef enum{
    NEW,
    READY,
    RUNNING,
    TERMINATED
} processState;

//CONTROLLER
typedef struct{
    int id;
    int arrivalTime;
    int runningTime;

    int startTime;
    int finishTime;

    int turnaroundTime;
    float weightedTurnaroundTime;
    int responseTime;

    int remainingTime;
    int waitingTime;

    int priority;
    processState state;
    pid_t pid; // PID of the process
} PC;

// Circular queue for RR
typedef struct {
    PC** data;
    int front;
    int rear;
    int count;
    int size;
} CircularQueue;

// Queue function prototypes
CircularQueue* createQueue(int capacity);
void destroyQueue(CircularQueue* q);
bool isFull(CircularQueue* q);
bool isEmpty(CircularQueue* q);
bool enqueue(CircularQueue *q, PC* p);
PC* dequeue(CircularQueue *q);       
PC* peek(CircularQueue* q);   

//MinHeap for SRTN and HPF
typedef struct {
    PC data[MAX_PROCESSES];
    int Heapsize;
} MinHeap;

//MinHeap functions prototypes
void initMinHeap(MinHeap *heap);
int HeapisFull(MinHeap *heap);
int HeapisEmpty(MinHeap *heap);
void insert(MinHeap *heap, PC* p);
PC extractMin(MinHeap *heap);
void heapify(MinHeap *heap, int index);
void swap(PC *a, PC *b);

// 
typedef struct{
    long mtype;
    PC process;
} message;

typedef struct { long mtype; int  count; } CountMsg;
typedef struct { long mtype; PC   process; } ProcMsg;
typedef struct {
    long mtype;          
    int  pid;            
} TermMsg;

//functions
void initScheduler();
void scheduleProcess(PC process);
void updateProcess(processState state, PC* process);
void calculator();
void logProcess(PC process, char *event);
void destroyScheduler();
void scheduleSRTN();
void scheduleRoundRobin(int totalProcesses, int quantum);
PC receiveProcess(int);
static void reap_children(int signo);

#endif

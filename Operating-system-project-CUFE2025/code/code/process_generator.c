#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <signal.h>
#include "scheduler.h" 
#include "buddy.c"       
#define MSG_KEY 1234

int msqid;
void clearResources(int signum)
{
    //TODO Clears all resources in case of interruption
    // 1. Remove the message queue
    if (msqid != -1) {
        msgctl(msqid, IPC_RMID, NULL);
    }
    
    // Kill all processes in the process group
    kill(0, SIGKILL);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, clearResources);
    signal(SIGTERM, clearResources);
    
    FILE *fp = fopen("processes.txt", "r");
    if (!fp) {
        perror("Error opening processes file");
        exit(-1);
    }
printf("////////////////////////////////////////////////////////////one prcesss entered");
    char line[128];
    PC processes[MAX_PROCESSES]; // Assuming a max of 100 processes
    int process_count = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#')
            continue;//goes to the next line
        
        sscanf(line, "%d\t%d\t%d\t%d\t%d", 
            &processes[process_count].id,
            &processes[process_count].arrivalTime,
            &processes[process_count].runningTime,
            &processes[process_count].priority,
            &processes[process_count].memSize
        );
        ////////////////////
// void* mem_start = allocate_memory(processes[process_count].memSize);
// if (mem_start == NULL) {
//     printf("Error: Not enough memory for process %d\n", processes[process_count].id);
//     continue;  // Skip process if no memory
// }

// processes[process_count].memPtr = mem_start;
// processes[process_count].memStart = (int)((char*)mem_start - memory); // compute start index

        ////////////////////
        process_count++;
    }
    fclose(fp);

    int algo_choice;
    printf("Choose Scheduling Algorithm:\n");
    printf("1. HPF\n2. SRTN\n3. RR\n");
    scanf("%d", &algo_choice);

    int quantum = 0;
    if (algo_choice == 3) {
        printf("Enter quantum: ");
        scanf("%d", &quantum);
    }

    // Create message queue first
    msqid = msgget(MSG_KEY, IPC_CREAT | 0666);  
    if (msqid < 0) { perror("msgget"); exit(1); }

    // Fork+exec the clock
    int clk_pid = fork();
    if (clk_pid < 0) {
        perror("fork clk"); exit(1);
    }
    if (clk_pid == 0) {
        execlp("./clk.out", "clk.out", NULL);
        perror("execlp clk"); exit(1);
    }
//     int lastClk = getClk();
// while (getClk() == lastClk);  // busy wait until clock advances
sleep(1); // wait for clock to start
    initClk();
    printf("[Generator] Clock now = %d\n", getClk());

    // Fork+exec the scheduler, passing algo name and optional quantum
    int sched_pid = fork();
    if (sched_pid < 0) {
        perror("fork scheduler"); exit(1);
    }
    if (sched_pid == 0) {
        if (algo_choice == 1) {
            execlp("./scheduler.out", "scheduler.out", "HPF", NULL);
        }
        else if (algo_choice == 2) {
            execlp("./scheduler.out", "scheduler.out", "SRTN", NULL);
        }
        else { // RR
            char quantum_str[16];
            sprintf(quantum_str, "%d", quantum);
            execlp("./scheduler.out",
                   "scheduler.out",
                   "RR",
                   quantum_str,
                   NULL);
        }
        // if we get here, exec failed
        perror("execlp scheduler");
        exit(1);
    }

    // Send process count to scheduler
    CountMsg cm = { .mtype = 1, .count = process_count };
    if (msgsnd(msqid, &cm, sizeof(cm.count), 0) == -1) {
        perror("msgsnd(count)"); exit(1);
    }

    // Send processes to scheduler
    for (int i = 0; i < process_count; i++) {
        // wait for arrival
        while (getClk() < processes[i].arrivalTime); //sleep 1 removed from here to avoid shifting the arrival time

        ProcMsg pm = { .mtype = 2, .process = processes[i] };
        if (msgsnd(msqid, &pm, sizeof(pm.process), 0) == -1) {
            perror("msgsnd(proc)"); exit(1);
        }
        printf("Process %d sent to scheduler at time %d\n", processes[i].id, getClk());
    }

    // Wait for scheduler to finish
    int wait_status;
    waitpid(sched_pid, &wait_status, 0); // wait for scheduler to finish
    if (WIFEXITED(wait_status)) {
        printf("Scheduler exited with status %d\n", WEXITSTATUS(wait_status));
    } else {
        printf("Scheduler terminated abnormally\n");
    }

    // Kill clock process
    kill(clk_pid, SIGKILL);
    waitpid(clk_pid, NULL, 0);

    // Clean up message queue
    msgctl(msqid, IPC_RMID, NULL);

    destroyClk(true);
    return 0;
}



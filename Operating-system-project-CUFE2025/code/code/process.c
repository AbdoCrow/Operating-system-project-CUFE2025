#include "scheduler.h"

/* Modify this file as needed*/
int remaining;

int main(int agrc, char * argv[])
{
    initClk();
    remaining = atoi(argv[1]); //get remaining time from scheduler
    printf("[Process %d] Started with remaining=%d\n", getpid(), remaining);
    fflush(stdout);//////////////////////
    while (remaining > 0)
    {
        printf("[Process %d] Tick, remaining=%d\n", getpid(), remaining);
        fflush(stdout);
        sleep(1);
        
        remaining--;
        
    }
    printf("[Process %d] Finished!\n", getpid());
    fflush(stdout);///////////////////////////
    //destroyClk(false);
    int msqid = msgget(1234, 0666);
    if (msqid != -1) {
        TermMsg tm = { .mtype = 3, .pid = getpid() };
        msgsnd(msqid, &tm, sizeof(tm.pid), 0);   /* fire‑and‑forget */
    }
    exit(0);
    return 0;
}

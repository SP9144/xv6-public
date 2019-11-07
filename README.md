# Usage

```c
make clean
make qemu SCHEDPOLICY=<FLAG>
```

FLAG can be FCFS, PRIORITY, MLFQ, DEFAULT


# Task1

## waitx

* The two arguments are pointers to integers to which waitx will assign the total
number of clock ticks during which process was waiting and total number of clock
ticks when the process was running. 
* The return values for waitx should be same as
that of wait system-call. 
* rtime,etime,ctime,iotime are added to the struct proc and updated at appropriate positions

```c
status = waitx(&a, &b);
```

## getpinfo

* Arguments are pointer to struct proc_stat type and pid of process

* This routine returns some basic information about each process: its process ID,
total run time, how many times it has been chosen to run, which queue it is currently
on 0, 1, 2, 3 or 4 and ticks received in each queue.

* Values are passed into a struct of type proc_stat

* Same fields added to struct proc and updated at appropriate sections of code

* returns 0 on success, -1 on failure

```c
getpinfo(&p, pid)
```

# Task 2

## FCFS

* Set SCHEDPOLICY=FCFS

* This is a non premptive policy.

* It selects process that was created earliest to run

```c
for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE)
          continue;
        
        else if(ptorun==0) 
          ptorun=p;

        else if(p->ctime<ptorun->ctime)
          ptorun=p;
      }
```

## PRIORITY

* Set SCHEDPOLICY=PRIORITY

* It selects the process with the highest priority for execution. In case two or more processes have the same priority, we choose them in a round-robin fashion.

* The priority of a process can be in the range [0,100], the smaller value will represent higher priority. The default priority of a process is 60.

* The new system call ​'set_priority' can change the priority of a process. The system-call returns the old-priority value of the process.

```c
for(p1 = ptable.proc; p1 < &ptable.proc[NPROC]; p1++){
    if(p1->state != RUNNABLE)
        continue;
    else if (ptorun->priority>p1->priority )
        ptorun = p1;
}
```

## MLFQ

* Set SCHEDPOLICY=MLFQ

* MLFQ scheduler allows processes to move between different priority queues based on their behavior and CPU bursts. If a process uses too much CPU time, it is pushed to a lower priority queue, leaving I/O bound and interactive processes for higher priority queues. Also, to prevent starvation, it implements aging.

* The struct proc stores the required details.

```c
struct queue{
  struct proc *proc[NPROC];
  int rtime;   //slice time
  int wtime;   //max wait time(for aging)
  int num;//size of queue, proc[num] is where new process wil go
};
```

* First aging is implemented by checking which process is exceeding waittime. This is done by comparing the ticks with the time since which it was created. If exceeded pushed up.

```c
else if(ticks-mlfq[it].proc[jt]->intime>mlfq[it].wtime && it>0){
              //now putting process in the queue newq 
              int newq=it-1;
              mlfq[newq].proc[mlfq[newq].num]=mlfq[it].proc[jt];
              mlfq[newq].proc[mlfq[newq].num]->intime=ticks;
              mlfq[newq].proc[mlfq[newq].num]->ticksnow[newq]=0;
              mlfq[newq].proc[mlfq[newq].num++]->current_queue=newq;

              //now time to delete from the queue
              for(int itt=jt+1;itt<mlfq[it].num;itt++) mlfq[it].proc[itt-1]=mlfq[it].proc[itt];
              mlfq[it].num--;
              mlfq[it].proc[mlfq[it].num]=0;

              ptorun=mlfq[newq].proc[mlfq[newq].num-1];
              // cprintf("%d shifted up to %d\n",ptorun->pid,ptorun->current_queue);
}
```

* Chooses the process in highest queue that is runnable

```c
for(i=0;i<5;i++){
        for(j=0;j<mlfq[i].num;j++){ 
          if(mlfq[i].proc[j]!=0){
            //Now checking process in queue i
            if(mlfq[i].proc[j]->state != RUNNABLE)  //change to priority round robin
              continue;
            else {
              ptorun=mlfq[i].proc[j];  //so now ptorun pointing to some address in ptable
              break;
            }
          }
        }
        if(ptorun!=0) break;
}
```

* Then checks if it has exceeded the time slice

```c
if(ptorun!=0 ){//has not been killed
        if(ptorun->killed==0){       
          int newq=i; 
          if(i<4 && 
          mlfq[i].rtime<=ptorun->ticksnow[ptorun->current_queue]) {  //did it exceed the time slice?
            newq=i+1;
          }

          //now putting process in the queue newq 
          mlfq[newq].proc[mlfq[newq].num]=ptorun;
          if(newq==i+1) {
            mlfq[newq].proc[mlfq[newq].num]->ticksnow[newq]=0;
            // cprintf("%d shifted down to %d\n",ptorun->pid,newq);
          }
          mlfq[newq].proc[mlfq[newq].num]->intime=ticks;    //since time since it was waiting should reset
          mlfq[newq].proc[mlfq[newq].num++]->current_queue=newq;
        }
        }

        //now time to delete from the queue
        for(int it=j+1;it<mlfq[i].num;it++) mlfq[i].proc[it-1]=mlfq[i].proc[it];
        mlfq[i].num--;
        mlfq[i].proc[mlfq[i].num]=0;
}        
```

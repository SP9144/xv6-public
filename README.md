# Usage

```c
make clean
make qemu SCHEDPOLICY=<FLAG>
```

FLAG can be FCFS, PRIORITY, MLFQ, DEFAULT


# New Syscalls

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

# Scheduling Algorithms

## FCFS

* Set SCHEDPOLICY=FCFS

* This is a non premptive policy.

* It selects process that was created earliest to run


## PRIORITY

* Set SCHEDPOLICY=PRIORITY

* It selects the process with the highest priority for execution. In case two or more processes have the same priority, we choose them in a round-robin fashion.

* The priority of a process can be in the range [0,100], the smaller value will represent higher priority. The default priority of a process is 60.

* The new system call ​'set_priority' can change the priority of a process. The system-call returns the old-priority value of the process.


## MLFQ

* Set SCHEDPOLICY=MLFQ

* MLFQ scheduler allows processes to move between different priority queues based on their behavior and CPU bursts. If a process uses too much CPU time, it is pushed to a lower priority queue, leaving I/O bound and interactive processes for higher priority queues. Also, to prevent starvation, it implements aging.

* The struct proc stores the required details.

* First aging is implemented by checking which process is exceeding waittime. This is done by comparing the ticks with the time since which it was created. If exceeded pushed up.

* Chooses the process in highest queue that is runnable``

* Then checks if it has exceeded the time slice

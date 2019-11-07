#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "mmu.h"
#include "param.h"
#include "proc.h"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf(2,"Usage: pinfo <pid>");
    }
    else
    {
        struct proc_stat p;
        p.pid = -1;
        p.num_run = -1;
        p.runtime = -1;
        int t=getpinfo(&p, atoi(argv[1]));
        if(t==-1)
            printf(1, "No such process\n");
        else if (t==-2)
            printf(1, "Invalid pid\n");
        else{
            printf(1, "pid=%d\nruntime=%d\nnum_run=%d\n", p.pid, p.runtime, p.num_run);
            printf(1,"current_queue=%d\n",p.current_queue);
            for(int i=0;i<5;i++)printf(1,"ticks[%d]=%d\n",i,p.ticks[i]);
        }
    }
    exit();
}
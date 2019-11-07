#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char *argv[])
{
    int pid=atoi(argv[1]);
    int pri=atoi(argv[2]);
    printf(1,"priority of %d changed from %d to %d\n",pid,setpriority(pri,pid),pri);
    //ps();
    exit();
}

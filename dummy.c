#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
// #include "types.h"
// #include "x86sss.h"
// #include "defs.h"

int main(int argc, char *argv[])
{
    int pid;
    pid = fork();
    if (pid == 0){
        for(volatile long long int i=0;i<=10000000*atoi(argv[1]);i++);
    //sleep(atoi(argv[1]));
    printf(1,"Done %d\n",atoi(argv[1]));
    }
    else{
        // wait();
    }
    // for(int i=0;i<3;i++){
    //     int pid=fork();
    //     if(pid==0)
    //     {
    //         for(volatile long long int i=0;i<=10000000;i++);            
    //     }
    // }

    // int ticksi=ticks;
    
    exit();
}
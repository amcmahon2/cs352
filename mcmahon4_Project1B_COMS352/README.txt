Andrew McMahon - 468507087

Description of work:

Files modified/added:
    kernel side:
        kernel/syscall.h: added system call numbers for the 3 functions
        kernel/syscall.c: added system call declarations and function pointters for the 3 functions
        kernel/proc.h: added count fields to proc to hold number of times the process has done some action (ex. made a system cal)
        kernel/proc.c: added resets to runCount and sleepCount in function freeproc
                        incremented runCount in function scheduler
                        incremented sleepCount in function sleepCount
        kernel/trap.c: incremented trapCount, systemcallCount, interruptCount, and preemptCount in function usertrap
                        added proc to function kerneltrap to allow for incrementation of preemptCount in the same function (kerneltrap)
        kernel/sysproc.c: implemented functions sys_getppid, sys_ps, and sys_getschedhistory
    user side:
        user/user.pl: added new system call to script, which autogenerates the required assembly code
        user/user.h: added given system calls to "specify the function interface for user programs"
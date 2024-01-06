Name - Sujay Chandra Shekara Sharma
CS Login - sujaycs@cs.wisc.edu
Wisc ID - 9085871714
NetID - schandrashe5
Email - schandrashe5@wisc.edu
Implementation Status - Everything is working

List of files modified:
1. defs.h - Add definitions for new helper functions
2. Makefile - Disable compiler optimization, set number of CPUs to 1 and update list of user level programs
3. nice.c - New user level program that tests the nice() system call
4. getschedstate.c - New user level program that tests the getschedstate() system call
5. plotGraphs.py - Python file to plot the graphs for RR and MLFQ scheduler for test 13
6. proc.c - Update the scheduler logic to be MLFQ and define helper functions
7. proc.h - Update the proc structure to also store scheduling related info
8. psched.h - Define the structure pschedinfo that is to be populated in the getschedstate() system call
9. syscall.c - Add system call function prototypes and map it to the new system calls
10. syscall.h - Define the system call numbers for the new system calls
11. sysfile.c - Implement the nice() and getschedstate() system calls
12. sysproc.c - Update the behaviour of sleep() so that it doesn't interfere with our MLFQ
13. test13CustomScheduler.txt - Output of running test 13 on MLFQ which is used for generating the graph
14. test13RoundRobinScheduler.txt - Output of running test 13 on RR which is used for generating the graph
15. trap.c - Add code to recalculate process priorities every 100 ticks
16. user.h - Define the nice() and getschedstate() system calls so that it can be used in user level programs
17. usys.S - Call the system call macro to enable user level code to invoke the new system calls
18. xv6_p4_mlfq_evaluation.png - MLFQ graph for test 13 with all 7 processes
19. xv6_p4_mlfq_evaluation_v2.png - MLFQ graph for test 13 with P4-P7
20. xv6_p4_rr_evaluation.png - RR graph for test 13 with all 7 processes
21. xv6_p4_rr_evaluation_v2.png - RR graph for test 13 with P4-P7

Resources used:
Remzi's Discussion Video on XV6 Scheduling - https://www.youtube.com/watch?v=eYfeOT1QYmg&ab_channel=RemziArpaci-Dusseau
ChatGPT - Queries around plotting the graphs, passing arguments to system calls, priority recalculation and modifying sleep behaviour

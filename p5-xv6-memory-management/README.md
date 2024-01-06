Group - 149
Names - Sujay Chandra Shekara Sharma, Una Chan
CS Login - sujaycs@cs.wisc.edu, hei@cs.wisc.edu 
Wisc ID - 9085871714, 9083655614
NetID - schandrashe5, hchan46
Emails - schandrashe5@wisc.edu, hchan46@wisc.edu
Implementation Status - Everything is working

List of files modified:
1. defs.h - Add definitions for new helper functions
2. Makefile - Update list of user level programs
3. mmap.c - New user level program that tests the mmap() system call
4. munmap.c - New user level program that tests the munmap() system call
5. mmap.h - Header file with constants defined for various mmap flags
6. proc.c - Copy mapping metadata from parent to child in fork() and delete mappings in exit()
7. proc.h - Add structure for storing mapping metadata
8. trap.c - Add case for handling page faults
9. syscall.c - Add system call function prototypes and map it to the new system calls
10. syscall.h - Define the system call numbers for the new system calls
11. sysproc.c - Implement the mmap() and munmap() system calls
12. types.h - Add type for page table entry pte_t
13. user.h - Define the mmap() and munmap() system calls so that it can be used in user level programs
14. usys.S - Call the system call macro to enable user level code to invoke the new system calls
15. vm.c - Helper functions for mmap() and munmap()

Resources used:
XV6 Manual - https://pdos.csail.mit.edu/6.828/2018/xv6/book-rev11.pdf
ChatGPT - Queries around how to allocate physical memory, how to create a mapping to virtual memory, how to read and write from file etc.

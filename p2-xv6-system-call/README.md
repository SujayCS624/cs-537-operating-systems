Name - Sujay Chandra Shekara Sharma
CS Login - sujaycs@cs.wisc.edu
Wisc ID - 9085871714
NetID - schandrashe5
Email - schandrashe5@wisc.edu
Implementation Status - Everything is working

List of files modified:
1. Makefile - Update list of user level programs and add getlastcat to it.
2. proc.h - Declared the global variable last_cat_arg.
3. proc.c - Defined the global variable last_cat_arg and initialised it to "Cat has not yet been called".
4. syscall.h - Define the system call number for new getlastcat system call.
5. syscall.c - Add system call function prototype for getlastcat and map it to the new system call number defined in syscall.h.
6. user.h - Define the getlastcat system call here so that it can be used in the user level program.
7. usys.S - Call the system call macro with getlastcat to enable user level code to invoke the new system call.
8. sysfile.c - Implement the sys_getlastcat() system call and also modify sys_read() and sys_open() to help keep track of the last argument passed to the cat command.
9. getlastcat.c (Newfile) - New user level program that calls sys_getlastcat() system call and prints result in desired format. 

Resources:
GeeksForGeeks - https://www.geeksforgeeks.org/xv6-operating-system-adding-a-new-system-call/ (Process to create a new system call and user level program)
ChatGPT - https://chat.openai.com/share/700f54b2-54fa-4b05-82f6-1b311f5adaa9 (Queries around passing arguments to system calls, printing something within a system call for debugging, explanation of sys_open and the code snippet for checking if a file doesn't exist)
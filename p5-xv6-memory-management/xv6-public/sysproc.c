#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "mmap.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// System call to perform memory mapping
void* 
sys_mmap(void)
{
  int virtualAddress;
  int length;
  int prot;
  int flags;
  int fd;
  int offset;

  // Read arguments of mmap into variables declared above
  if(argint(0, &virtualAddress)) {
    cprintf("Failed to read addr argument\n");
    return (void*)-1;
  }
  else if(argint(1, &length) < 0) {
    cprintf("Failed to read length argument\n");
    return (void*)-1;
  }
  else if(argint(2, &prot) < 0) {
    cprintf("Failed to read prot argument\n");
    return (void*)-1;
  }
  else if(argint(3, &flags) < 0) {
    cprintf("Failed to read flags argument\n");
    return (void*)-1;
  }
  else if(argint(4, &fd) < 0) {
    cprintf("Failed to read fd argument\n");
    return (void*)-1;
  }
  else if(argint(5, &offset) < 0) {
    cprintf("Failed to read offset argument\n");
    return (void*)-1;
  }
  // Check if port argument is invalid
  else if(prot != (PROT_READ | PROT_WRITE)) {
    cprintf("Invalid prot argument\n");
    return (void*)-1;
  }
  // Check if flags argument is invalid
  else if(((flags & MAP_SHARED) == 0) && ((flags & MAP_PRIVATE) == 0)) {
    cprintf("Invalid flags argument: Atleast one of MAP_SHARED or MAP_PRIVATE must be present\n");
    return (void*)-1;
  }
  // Check if flags argument is invalid
  else if(flags & MAP_SHARED && flags & MAP_PRIVATE) {
    cprintf("Invalid flags argument: Both MAP_SHARED and MAP_PRIVATE can't be present\n");
    return (void*)-1;
  }
  // Check if offset argument is invalid in case of file backed mapping
  else if(offset != 0 && ((flags & MAP_ANONYMOUS) == 0)) {
    cprintf("Invalid offset argument\n");
    return (void*)-1;
  }
  // Check if virtual address is a multiple of page size for fixed mapping
  else if(virtualAddress % PGSIZE != 0 && ((flags & MAP_FIXED))) {
    cprintf("Invalid addr argument: %d\n", virtualAddress);
    return (void*)-1;
  }

  // Calculate number of pages that need to be allocated
  int numPages = length / PGSIZE;
  if(length % PGSIZE != 0) {
    numPages++;
  }

  // cprintf("Number of Pages to Allocate: %d\n", numPages);

  // Allocate pages in physical memory
  char *physicalPages[numPages];
  for(int i = 0; i < numPages; i++) {
    physicalPages[i] = kalloc();
    // cprintf("Physical Page Address %d: %p\n", i, physicalPages[i]);
    // Error in allocating page in physical memory
    if (physicalPages[i] == 0) {
        for (int j = 0; j < i; j++) {
            kfree(physicalPages[j]);
        }
        cprintf("Failed to allocate %d pages in physical memory\n", numPages);
        return (void *)-1;
    }
  }

  // cprintf("Allocation Successful\n");

  // Set virtual address and current process's page directory address
  void *currentVirtualAddress = (void*)0x60000000;
  pte_t *currentPageDirectoryAddress = myproc()->pgdir;

  // Update virtual address to argument provided in the case of fixed mapping
  if(flags & MAP_FIXED) {
    currentVirtualAddress = (void*)virtualAddress;
    // Check that virtual address is within bounds
    if(currentVirtualAddress < (void*)0x60000000 || currentVirtualAddress >= (void*)KERNBASE) {
      cprintf("Provided address out of bounds\n");
      return (void *)-1;
    }
    // Check that mapping doesn't already exist at virtual address
    else if(canallocatva(currentPageDirectoryAddress, currentVirtualAddress, numPages) == -1) {
      cprintf("Unable to allocate %d pages at %p\n", numPages, currentVirtualAddress);
      return (void *)-1;
    }
  }
  // If not fixed mapping, find virtual address to perform mapping
  else {
    int canAllocate = -1;
    for(;currentVirtualAddress < (void*)KERNBASE;) {
      if(canallocatva(currentPageDirectoryAddress, currentVirtualAddress, numPages) == -1) {
        currentVirtualAddress += PGSIZE;
      }
      else {
        canAllocate = 0;
        virtualAddress = (uint)currentVirtualAddress;
        break;
      }
    }
    // Couldn't find any virtual address to perform mapping at
    if(canAllocate == -1) {
      cprintf("Unable to allocate %d pages\n", numPages);
      return (void *)-1;
    }
  }

  void* guardPageVirtualAddress = (void*)0;

  // If grows up flag is set, update guard page virtual address and ensure that that address is unmapped
  if(flags & MAP_GROWSUP) {
    guardPageVirtualAddress = (void*)(virtualAddress + (numPages*PGSIZE));
    if(canallocatva(currentPageDirectoryAddress, guardPageVirtualAddress, 1) == -1) {
      cprintf("Unable to allocate %d pages at %p\n", numPages, currentVirtualAddress);
      return (void *)-1;
    };
    // cprintf("Guard Page Address: %p\n", guardPageVirtualAddress);
  }

  // Map pages in physical memory allocated earlier to the virtual addresses starting from currentVirtualAddress
  for(int i = 0; i < numPages; i++) {
    int mapResult = mappageswrapper(currentPageDirectoryAddress, currentVirtualAddress, PGSIZE,V2P(physicalPages[i]), PTE_W|PTE_U);
    // Failed to perform mapping for some reason
    if(mapResult != 0) {
      cprintf("Failed to map physical address to virtual address\n");
      return (void *)-1;
    }
    // cprintf("Mapped Page %d at %p\n", i+1, currentVirtualAddress);
    currentVirtualAddress += PGSIZE;
  }

  // Read contents of file into mapped virtual address in case of file backed mapping
  if((flags & MAP_ANONYMOUS) == 0) {
    char buffer[(numPages * PGSIZE) + 1];
    // Set offset within file to 0
    myproc()->ofile[fd]->off = 0;
    struct file* file = myproc()->ofile[fd];
    currentVirtualAddress = (void*)virtualAddress;
    int bytesRead = fileread(file, buffer, numPages*PGSIZE);
    if(bytesRead < 0) {
      cprintf("Unable to read from file\n");
      return (void*)-1;
    }
    memmove(currentVirtualAddress, buffer, bytesRead);
  }  

  // Store mapping metadata in process structure
  int currentMappingIndex = myproc()->currentMappingIndex;
  myproc()->mapEntries[currentMappingIndex].flags = flags;
  myproc()->mapEntries[currentMappingIndex].isValid = 0;
  myproc()->mapEntries[currentMappingIndex].numPages = numPages;
  myproc()->mapEntries[currentMappingIndex].fd = fd;
  myproc()->mapEntries[currentMappingIndex].startingVirtualAddress = (void*)virtualAddress;
  myproc()->mapEntries[currentMappingIndex].guardPageVirtualAddress = (void*)guardPageVirtualAddress;
  myproc()->currentMappingIndex++;
  // cprintf("Inside mmap\n");
  // cprintf("%p %d %d %d %d %d\n", virtualAddress, length, prot, flags, fd, offset);
  return (void*)virtualAddress;
}

// System call to perform memory unmapping
int sys_munmap(void)
{
  int virtualAddress;
  int length;
  // Read arguments of munmap into variables declared above
  if(argint(0, &virtualAddress)) {
    cprintf("Failed to read addr argument\n");
    return -1;
  }
  else if(argint(1, &length) < 0) {
    cprintf("Failed to read length argument\n");
    return -1;
  }
  // Check if virtual address is a multiple of page size
  else if(virtualAddress % PGSIZE != 0) {
    cprintf("Invalid addr argument: %d\n", virtualAddress);
    return -1;
  }

  // Calculate number of pages that need to be unmapped
  int numPages = length / PGSIZE;
  if(length % PGSIZE != 0) {
    numPages++;
  }

  // cprintf("Number of Pages to Deallocate: %d\n", numPages);

  // Set virtual address and current process's page directory address
  void *currentVirtualAddress = (void*)virtualAddress;
  pte_t *currentPageDirectoryAddress = myproc()->pgdir;

  // Find mapping metadata in process structure
  int mappingIndex = findmappingmetadata(currentVirtualAddress);
  if(mappingIndex == -1) {
    cprintf("Unable to find mapping metadata\n");
    return -1;
  }

  // Retrieve mapping metadata from structure
  int flags = myproc()->mapEntries[mappingIndex].flags;
  int fd = myproc()->mapEntries[mappingIndex].fd;
  int numPagesAllocated = myproc()->mapEntries[mappingIndex].numPages;
  
  // cprintf("Number of Pages Allocated: %d\n", numPagesAllocated);

  // Write contents of mapped virtual address to file in the case of file backed mapping which isn't private
  if((flags & MAP_ANONYMOUS) == 0 && (flags & MAP_PRIVATE) == 0) {
    // Set offset within file to 0
    myproc()->ofile[fd]->off = 0;
    struct file* file = myproc()->ofile[fd];
    // cprintf("%p\n", file);
    int bytesWritten = filewrite(file, currentVirtualAddress, numPagesAllocated * PGSIZE);
    if(bytesWritten < 0) {
      cprintf("Unable to write from file\n");
      return -1;
    }
    // cprintf("Bytes Written: %d\n", bytesWritten);
  }

  // Remove page table entries and free corresponding physical memory
  for(int i = 0; i < numPages; i++) {
    int res = clearpte(currentPageDirectoryAddress, currentVirtualAddress);
    if(res == -1) {
      cprintf("Error in removing the mapping for virtual address: %p\n", currentVirtualAddress);
      return -1;
    }
    currentVirtualAddress += PGSIZE;
  }  

  // Clear mapping metadata from process structure
  myproc()->mapEntries[mappingIndex].flags = 0;
  myproc()->mapEntries[mappingIndex].isValid = -1;
  myproc()->mapEntries[mappingIndex].numPages = 0;
  myproc()->mapEntries[mappingIndex].fd = 0;
  myproc()->mapEntries[mappingIndex].startingVirtualAddress = (void*)0;
  myproc()->mapEntries[mappingIndex].guardPageVirtualAddress = (void*)0;  

  // cprintf("Inside munmap\n");
  // cprintf("%p %d\n", virtualAddress, length);
  return 0;
}

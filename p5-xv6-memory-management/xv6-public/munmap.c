#include "types.h"
#include "stat.h"
#include "user.h"
int main(int argc, char **argv) 
{
    void *addr = (void*)0x60020000;
    int arg = atoi(argv[1]);
    int res = munmap(addr, arg);
    printf(1, "%d\n", res);
    exit();
}

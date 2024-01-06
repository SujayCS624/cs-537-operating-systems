#include "types.h"
#include "stat.h"
#include "user.h"
int main(int argc, char **argv) 
{
    int niceVal = atoi(argv[1]);
    int res = nice(niceVal);
    printf(1, "%d\n", res);
    exit();
}

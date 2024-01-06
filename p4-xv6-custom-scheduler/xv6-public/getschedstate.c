#include "types.h"
#include "stat.h"
#include "user.h"
#include "psched.h"
int main(void) 
{
    struct pschedinfo temp = {};
    int res = getschedstate(&temp);
    printf(1, "%d\n", res);
    exit();
}

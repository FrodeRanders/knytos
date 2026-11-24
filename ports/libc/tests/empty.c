#include <stdio.h>

/* Empty linker callback ranges must not execute a neighbouring object. */
int main(void) {
    puts("CABI PASS case=8");
    return 0;
}

#include "config.h"
#include <stdio.h>
#include "erl_printf.h"
static int (*volatile entry)(int,const char*,...) = erts_fdprintf;
int main(void) { return entry == 0; }

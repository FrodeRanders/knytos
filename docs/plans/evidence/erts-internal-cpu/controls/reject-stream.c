#include "config.h"
#include <stdio.h>
#include "erl_printf.h"
static int (*volatile entry)(const char*,...) = erts_printf;
int main(void) { return entry == 0; }

#include "config.h"
#include "erl_misc_utils.h"
static int (*volatile entry)(long) = erts_milli_sleep;
int main(void) { return entry == 0; }

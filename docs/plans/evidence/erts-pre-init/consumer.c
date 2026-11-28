#include "config.h"
#include "sys.h"
static void (*volatile entry)(void) = erts_sys_pre_init;
int main(void) { return entry == 0; }

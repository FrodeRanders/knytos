#include "config.h"
#include "sys.h"
#include "global.h"
#include "erl_cpu_topology.h"
#include "erl_misc_utils.h"
#include "erl_printf.h"
static void (*volatile entry)(int*,int*,int*,int*,int*,int*) = erts_pre_early_init_cpu_topology;
static int (*volatile format)(char*,size_t,const char*,...) = erts_snprintf;
static int (*volatile stream)(const char*,...) = erts_printf;
int main(void) { return entry == 0 || format == 0 || stream == 0; }

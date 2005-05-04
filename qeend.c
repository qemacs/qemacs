#include <stdlib.h>
#include "qe.h"

#ifdef CONFIG_INIT_CALLS

/* add empty pointers at the end of init and cleanup sections */
static int (*__initcall_end)(void) __init_call = NULL;

static void (*__exitcall_end)(void) __exit_call = NULL;

#endif

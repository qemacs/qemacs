#define CONFIG_TINY 1
#include "qe.c"
#include "util.c"
#include "cutils.c"
#include "charset.c"
#include "buffer.c"
#include "search.c"
#include "parser.c"
#include "input.c"
#include "display.c"
#include "hex.c"
#include "list.c"

#ifdef CONFIG_WIN32
#include "unix.c"
#include "win32.c"
#else
#include "unix.c"
#include "tty.c"
#endif

#ifdef CONFIG_INIT_CALLS
#include "qeend.c"
#endif

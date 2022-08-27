#define CONFIG_TINY 1
#include "qe.c"
#include "util.c"
#include "cutils.c"
#include "charset.c"
#include "buffer.c"
#include "search.c"
#include "input.c"
#include "display.c"
#include "modes/hex.c"
#include "parser.c"

#ifdef CONFIG_WIN32
#include "unix.c"
#include "win32.c"
#else
#include "unix.c"
#include "tty.c"
#endif

#include "tqe_modules.c"

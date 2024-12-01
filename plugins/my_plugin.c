/*
 * Simple plugin example
 */
#include "qe.h"

/* insert 'hello' at the current cursor position */
static void insert_hello(EditState *s)
{
    s->offset += eb_insert_str(s->b, s->offset, "Hello world\n");
}

static const CmdDef my_commands[] = {
    CMD2( "insert-hello", "C-c h",
          "Insert the string Hello world\\n",
          insert_hello, ES, "*")
};

static int my_plugin_init(QEmacsState *qs) {
    /* commands and default keys */
    qe_register_commands(qs, NULL, my_commands, countof(my_commands));
    return 0;
}

qe_module_init(my_plugin_init);

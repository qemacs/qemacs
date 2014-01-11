/*
 * Simple plugin example
 */
#include "qe.h"

/* insert 'hello' at the current cursor position */
static void insert_hello(EditState *s)
{
    s->offset += eb_insert_str(s->b, s->offset, "Hello world\n");
}

static CmdDef my_commands[] = {
    CMD2( KEY_CTRLC('h'), KEY_NONE,     /* C-c h */
          "insert-hello", insert_hello, ES, "*")
    CMD_DEF_END,
};

static int my_plugin_init(void)
{
    /* commands and default keys */
    qe_register_cmd_table(my_commands, NULL);

    return 0;
}

qe_module_init(my_plugin_init);

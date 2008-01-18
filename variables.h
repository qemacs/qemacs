
typedef enum QVarType {
    VAR_UNKNOWN = 0,
    VAR_NUMBER,
    VAR_STRING,
    VAR_CHARS,
} QVarType;

enum QVarAccess {
    VAR_RO,
    VAR_RW,
};

enum QVarDomain {
    VAR_GLOBAL,
    VAR_STATE,
    VAR_BUFFER,
    VAR_WINDOW,
    VAR_MODE,
    VAR_SELF,
};

typedef struct VarDef {
    const char *name;
    enum QVarDomain domain : 4;
    enum QVarType type : 4;
    enum QVarAccess rw : 2;
    unsigned int size : 16;
    union {
        void *ptr;
        int offset;
        char **pstr;
        int *pint;
        char *str;
        int num;
    } value;
    struct VarDef *next;
} VarDef;

#define U_VAR(name, type) \
    { (name), VAR_SELF, type, VAR_RW, 0, { .num = 0 }, NULL },
#define G_VAR(name,ptr,type,rw) \
    { (name), VAR_GLOBAL, type, rw, 0, { (void*)&(ptr) }, NULL },
#define S_VAR(name,fld,type,rw) \
    { (name), VAR_STATE, type, rw, sizeof(((QEmacsState*)0)->fld), { .offset = offsetof(QEmacsState, fld) }, NULL },
#define B_VAR(name,fld,type,rw) \
    { (name), VAR_BUFFER, type, rw, sizeof(((EditBuffer*)0)->fld), { .offset = offsetof(EditBuffer, fld) }, NULL },
#define W_VAR(name,fld,type,rw) \
    { (name), VAR_WINDOW, type, rw, sizeof(((EditState*)0)->fld), { .offset = offsetof(EditState, fld) }, NULL },
#define M_VAR(name,fld,type,rw) \
    { (name), VAR_MODE, type, rw, sizeof(((ModeDef*)0)->fld), { .offset = offsetof(ModeDef, fld) }, NULL },

void qe_register_variables(VarDef *vars, int count);
VarDef *qe_find_variable(const char *name);
void qe_complete_variable(CompleteState *cp);

QVarType qe_get_variable(EditState *s, const char *name,
			 char *buf, int size, int *pnum);
QVarType qe_set_variable(EditState *s, const char *name,
			 const char *value, int num);

void qe_list_variables(EditState *s, EditBuffer *b);

void do_set_variable(EditState *s, const char *name, const char *value);
void do_show_variable(EditState *s, const char *name);

/*
 * Module for handling variables in QEmacs
 *
 * Copyright (c) 2000-2024 Charlie Gordon.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef QE_VARIABLES_H
#define QE_VARIABLES_H

typedef enum QVarType {
    VAR_UNKNOWN = 0,
    VAR_NUMBER,
    VAR_STRING,
    VAR_CHARS,
    VAR_READONLY,
    VAR_INVALID,
} QVarType;

enum QVarAccess {
    VAR_RO,
    VAR_RW,
    VAR_RW_SAVE,
};

enum QVarDomain {
    VAR_GLOBAL,
    VAR_STATE,
    VAR_BUFFER,
    VAR_WINDOW,
    VAR_MODE,
    VAR_SELF,
};

extern const char * const var_domain[];

typedef struct VarDef VarDef;
struct VarDef {
    const char *name;
    const char *desc;
    unsigned int var_alloc : 1;
    unsigned int str_alloc : 1;
    unsigned int modified : 1;
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
    QVarType (*set_value)(EditState *s, VarDef *vp, void *ptr,
                          const char *str, int num);
    VarDef *next;
};

#define U_VAR_F(name, type, fun, desc) \
    { (name), desc, 0, 0, 0, VAR_SELF, type, VAR_RW, 0, { .num = 0 }, fun, NULL },
#define G_VAR_F(name, var, type, rw, fun, desc) \
    { (name), desc, 0, 0, 0, VAR_GLOBAL, type, rw, 0, \
      { .ptr = (void*)&(var) }, fun, NULL },
#define S_VAR_F(name, fld, type, rw, fun, desc) \
    { (name), desc, 0, 0, 0, VAR_STATE, type, rw, sizeof(((QEmacsState*)0)->fld), \
      { .offset = offsetof(QEmacsState, fld) }, fun, NULL },
#define B_VAR_F(name, fld, type, rw, fun, desc) \
    { (name), desc, 0, 0, 0, VAR_BUFFER, type, rw, sizeof(((EditBuffer*)0)->fld), \
      { .offset = offsetof(EditBuffer, fld) }, fun, NULL },
#define W_VAR_F(name, fld, type, rw, fun, desc) \
    { (name), desc, 0, 0, 0, VAR_WINDOW, type, rw, sizeof(((EditState*)0)->fld), \
      { .offset = offsetof(EditState, fld) }, fun, NULL },
#define M_VAR_F(name, fld, type, rw, fun, desc) \
    { (name), desc, 0, 0, 0, VAR_MODE, type, rw, sizeof(((ModeDef*)0)->fld), \
      { .offset = offsetof(ModeDef, fld) }, fun, NULL },

#define U_VAR(name,type,desc)         U_VAR_F(name, type, NULL, desc)
#define G_VAR(name,var,type,rw,desc)  G_VAR_F(name, var, type, rw, NULL, desc)
#define S_VAR(name,fld,type,rw,desc)  S_VAR_F(name, fld, type, rw, NULL, desc)
#define B_VAR(name,fld,type,rw,desc)  B_VAR_F(name, fld, type, rw, NULL, desc)
#define W_VAR(name,fld,type,rw,desc)  W_VAR_F(name, fld, type, rw, NULL, desc)
#define M_VAR(name,fld,type,rw,desc)  M_VAR_F(name, fld, type, rw, NULL, desc)

void qe_register_variables(QEmacsState *qs, VarDef *vars, int count);
void variable_complete(CompleteState *cp, CompleteFunc enumerate);
int eb_variable_print_entry(EditBuffer *b, VarDef *vp, EditState *s);
int variable_print_entry(CompleteState *cp, EditState *s, const char *name);

QVarType qe_get_variable(EditState *s, const char *name,
                         char *buf, int size, int *pnum, int as_source);
QVarType qe_set_variable(EditState *s, const char *name,
                         const char *value, int num);

void qe_list_variables(EditState *s, EditBuffer *b);

void do_set_variable(EditState *s, const char *name, const char *value);
void do_show_variable(EditState *s, const char *name);
void qe_save_variables(EditState *s, EditBuffer *b);

#endif  /* QE_VARIABLES_H */

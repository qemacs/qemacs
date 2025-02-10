/*
 * Lua language mode for QEmacs.
 *
 * Copyright (c) 2000-2025 Charlie Gordon.
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

#include "qe.h"

/*---------------- Lua script coloring ----------------*/

static char const lua_keywords[] = {
    "|and|break|do|else|elseif|end|false|for|function|goto|if|in"
    "|local|nil|not|or|repeat|require|return|then|true|until|while"
    "|self"
    "|"
};

#if 0
static char const lua_tokens[] = {
    "|+|-|*|/|%|^|#|==|~=|<=|>=|<|>|=|(|)|{|}|[|]|::|;|:|,|...|..|.|"
};
#endif

enum {
    IN_LUA_COMMENT = 0x10,
    IN_LUA_STRING  = 0x20,
    IN_LUA_STRING2 = 0x40,
    IN_LUA_LONGLIT = 0x80,
    IN_LUA_LEVEL   = 0x0F,
};

enum {
    LUA_STYLE_TEXT =     QE_STYLE_DEFAULT,
    LUA_STYLE_COMMENT =  QE_STYLE_COMMENT,
    LUA_STYLE_STRING =   QE_STYLE_STRING,
    LUA_STYLE_LONGLIT =  QE_STYLE_STRING,
    LUA_STYLE_NUMBER =   QE_STYLE_NUMBER,
    LUA_STYLE_KEYWORD =  QE_STYLE_KEYWORD,
    LUA_STYLE_TYPE =     QE_STYLE_TYPE,
    LUA_STYLE_FUNCTION = QE_STYLE_FUNCTION,
};

static int lua_long_bracket(const char32_t *str, int *level) {
    int i;

    for (i = 1; str[i] == '='; i++)
        continue;
    if (str[i] == str[0]) {
        *level = i - 1;
        return 1;
    } else {
        return 0;
    }
}

static void lua_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    int i = 0, j, start = i, level = 0, level1, style;
    char32_t c, sep = 0;
    int state = cp->colorize_state;
    char kbuf[64];

    if (state & IN_LUA_LONGLIT) {
        /* either a comment or a string */
        level = state & IN_LUA_LEVEL;
        goto parse_longlit;
    }

    if (state & IN_LUA_STRING) {
        sep = '\'';
        state = 0;
        goto parse_string;
    }
    if (state & IN_LUA_STRING2) {
        sep = '\"';
        state = 0;
        goto parse_string;
    }

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '-':
            if (str[i] == '-') {
                if (str[i + 1] == '['
                &&  lua_long_bracket(str + i + 1, &level)) {
                    state = IN_LUA_COMMENT | IN_LUA_LONGLIT |
                            (level & IN_LUA_LEVEL);
                    goto parse_longlit;
                }
                i = n;
                SET_STYLE(sbuf, start, i, LUA_STYLE_COMMENT);
                continue;
            }
            break;
        case '\'':
        case '\"':
            /* parse string const */
            sep = c;
        parse_string:
            while (i < n) {
                c = str[i++];
                if (c == '\\') {
                    if (str[i] == 'z' && i + 1 == n) {
                        /* XXX: partial support for \z */
                        state = (sep == '\'') ? IN_LUA_STRING : IN_LUA_STRING2;
                        i += 1;
                    } else
                    if (i == n) {
                        state = (sep == '\'') ? IN_LUA_STRING : IN_LUA_STRING2;
                    } else {
                        i += 1;
                    }
                } else
                if (c == sep) {
                    break;
                }
            }
            SET_STYLE(sbuf, start, i, LUA_STYLE_STRING);
            continue;
        case '[':
            if (lua_long_bracket(str + i - 1, &level)) {
                state = IN_LUA_LONGLIT | (level & IN_LUA_LEVEL);
                goto parse_longlit;
            }
            break;
        parse_longlit:
            style = (state & IN_LUA_COMMENT) ?
                    LUA_STYLE_COMMENT : LUA_STYLE_LONGLIT;
            for (; i < n; i++) {
                if (str[i] == ']'
                &&  lua_long_bracket(str + i, &level1)
                &&  level1 == level) {
                    state = 0;
                    i += level + 2;
                    break;
                }
            }
            SET_STYLE(sbuf, start, i, style);
            continue;
        default:
            if (qe_isdigit(c)) {
                /* XXX: should parse actual number syntax */
                for (; i < n; i++) {
                    if (!qe_isalnum(str[i]) && str[i] != '.')
                        break;
                }
                SET_STYLE(sbuf, start, i, LUA_STYLE_NUMBER);
                continue;
            }
            if (qe_isalpha_(c)) {
                i += ustr_get_identifier(kbuf, countof(kbuf), c, str, i, n);
                if (strfind(syn->keywords, kbuf)) {
                    SET_STYLE(sbuf, start, i, LUA_STYLE_KEYWORD);
                    continue;
                }
                for (j = i; j < n && qe_isspace(str[j]); j++)
                    continue;
                /* function calls use parenthesized argument list or
                   single string or table literal */
                if (qe_findchar("('\"{", str[j])) {
                    SET_STYLE(sbuf, start, i, LUA_STYLE_FUNCTION);
                    continue;
                }
                if (syn->types
                &&  (strfind(syn->types, kbuf)
                ||   (qe_isupper(c) && qe_islower(kbuf[1])))) {
                    SET_STYLE(sbuf, start, i, LUA_STYLE_TYPE);
                    continue;
                }
                continue;
            }
            break;
        }
    }
    cp->colorize_state = state;
}

static ModeDef lua_mode = {
    .name = "Lua",
    .extensions = "lua",
    .shell_handlers = "lua",
    .keywords = lua_keywords,
    .colorize_func = lua_colorize_line,
};

static char const teal_keywords[] = {
    "|and|break|do|else|elseif|end|false|for|function|goto|if|in"
    "|local|nil|not|or|repeat|require|return|then|true|until|while"
    "|self|record|interface|enum|type|is|where"
    "|"
};

static char const teal_types[] = {
    // other types start with a capital letter
    "|any|boolean|integer|number|string|FILE"
    "|"
};

static ModeDef teal_mode = {
    .name = "Teal",
    .extensions = "tl",
    .shell_handlers = "tl",
    .keywords = teal_keywords,
    .types = teal_types,
    .colorize_func = lua_colorize_line,
};

static int lua_init(QEmacsState *qs)
{
    qe_register_mode(qs, &lua_mode, MODEF_SYNTAX);
    qe_register_mode(qs, &teal_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(lua_init);

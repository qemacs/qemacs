/*
 * CSS2 parser for qemacs.
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "qe.h"
#include "qfribidi.h"
#include "css.h"

//#define DEBUG

#ifdef DEBUG
#define dprintf(fmt, args...) printf(fmt, ##args)
#else
#define dprintf(fmt, args...)
#define NDEBUG
#endif
#define ddprintf(fmt, args...)

#include <assert.h>

static void css_error1(CSSParseState *b, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    css_error(b->filename, b->line_num, buf);
    va_end(ap);
}

/***********************************************************/
/* CSS property parser */

/* return a length and its unit. some normalization is done to limit
   the number of units. Return non zero if error. */
static int css_get_length(int *length_ptr, int *unit_ptr, const char *p)
{
    int num, len, unit;
    const char *p1;
    char buf[32];
    float f;

    p1 = p;
    if (*p == '+' || *p == '-')
        p++;
    while (isdigit((unsigned char)*p))
        p++;
    if (*p == '.') {
        p++;
        while (isdigit((unsigned char)*p))
        p++;
    }
    len = p - p1;
    if (len == 0)
        return -1;
    if (len > sizeof(buf) - 1)
        len = sizeof(buf) - 1;
    memcpy(buf, p1, len);
    buf[len] = '\0';
    f = strtod(buf, NULL);
    unit = css_get_enum(p, "px,%,ex,em,mm,in,cm,pt,pc");
    if (unit < 0) {
        /* only 0 is valid without unit */
        if (f != 0.0 || *p != '\0')
            return -1;
        num = 0;
        unit = CSS_UNIT_NONE;
    } else {
        unit++;
        if (f < 0 && unit != CSS_UNIT_PERCENT)
            return -1;
        switch(unit) {
        case CSS_UNIT_PIXEL:
            num = (int)(f);
            break;
        case CSS_UNIT_PERCENT:
            if (f < 0) {
                if (f >= 100)
                    return -1;
                f = 100 - f;
            }
            num = (int)(f * (0.01 * CSS_LENGTH_FRAC_BASE));
            break;
        case CSS_UNIT_EX:
        case CSS_UNIT_EM:
            num = (int)(f * CSS_LENGTH_FRAC_BASE);
            break;
        case CSS_UNIT_IN:
        phys_unit:
            unit = CSS_UNIT_IN;
            num = (int)(f * CSS_LENGTH_FRAC_BASE);
            break;
        case CSS_UNIT_MM:
            f = f / 25.4;
            goto phys_unit;
        case CSS_UNIT_CM:
            f = f / 2.54;
            goto phys_unit;
        case CSS_UNIT_PT:
            f = f / 72.0;
            goto phys_unit;
        case CSS_UNIT_PC:
            f = f * 12.0 / 72.0;
            goto phys_unit;
        default:
            return -1;
        }
    }
    *length_ptr = num;
    *unit_ptr = unit;
    return 0;
}

/* return the font size in CSS_UNIT_IN corresponding to size 0 <= i <= 6 */
/* XXX: change name */
int get_font_size(int i)
{
    int val;

    val = (14 * CSS_LENGTH_FRAC_BASE) / 72;
    i -= 2;
    /* compute 1.2^(i-2) */
    while (i > 0) {
        val = (val * 12) / 10;
        i--;
    }
    while (i < 0) {
        val = (val * 10) / 12;
        i++;
    }
    return val;
}

/* XXX: expand string dynamically */
/* XXX: end of line ? */
static char *css_parse_string(const char **pp)
{
    char buf[4096], *q;
    const char *p;
    int sep, c;

    p = *pp;
    q = buf;
    sep = *p++;
    for(;;) {
        c = *p++;
        if (c == sep || c == '\0')
            break;
        if (c == '\\') {
            c = *p++;
            if (c == 'A') {
                c = CSS_CONTENT_EOL;
            } 
            /* XXX: hex digits */ 
        }
        if ((q - buf) < sizeof(buf) - 1)
            *q++ = c;
    }
    *q = '\0';
    //    printf("string='%s'\n", buf);
    *pp = p;
    return strdup(buf);
}

/* add a given number of values */
void css_add_prop_values(CSSProperty ***last_prop, 
                         int property_index, 
                         int nb_values, CSSPropertyValue *val_ptr)
{
    CSSProperty *prop;

    prop = malloc(sizeof(CSSProperty) + 
                  (nb_values - 1) * sizeof(CSSPropertyValue));
    if (!prop)
        return;
    prop->property = property_index;
    prop->next = NULL;
    prop->nb_values = nb_values;
    memcpy(&prop->value, val_ptr, nb_values * sizeof(CSSPropertyValue));
    
    **last_prop = prop;
    *last_prop = &prop->next;
}

void css_add_prop(CSSProperty ***last_prop, 
                  int property_index, CSSPropertyValue *val_ptr)
{
    css_add_prop_values(last_prop, property_index, 1, val_ptr);
}

void css_add_prop_unit(CSSProperty ***last_prop, 
                       int property_index, int type, int val)
{
    CSSPropertyValue value;
    value.type = type;
    value.u.val = val;
    css_add_prop(last_prop, property_index, &value);
}

void css_add_prop_int(CSSProperty ***last_prop, 
                      int property_index, int val)
{
    css_add_prop_unit(last_prop, property_index, CSS_UNIT_NONE, val);
}

static const char border_style_enum[] = 
"none,hidden,dotted,dashed,solid,double,groove,ridge,inset,outset";
static const char list_style_enum[] = 
"disc,circle,square,decimal,lower-alpha,upper-alpha,lower-roman,upper-roman,none";

#define MAX_ARGS 32

/* parse the properties and return a list of properties. NOTE: 'b' is
   only used for error reporting */
CSSProperty *css_parse_properties(CSSParseState *b, const char *props_str)
{
    const char *p;
    char property[64];
    char buf[1024], buf2[64];
    int property_index, type, val, nb_args, i, unit;
    int property_index1;
    CSSPropertyValue args[MAX_ARGS];
    CSSProperty **last_prop, *first_prop;
    const CSSPropertyDef *def;

    first_prop = NULL;
    last_prop = &first_prop;
    p = props_str;
    for(;;) {
        skip_spaces(&p);
        if (*p == '\0') 
            break;
        get_str(&p, property, sizeof(property), " :");
        if (*p == '\0') 
            break;
        skip_spaces(&p);
        if (*p == ':')
            p++;
        skip_spaces(&p);
        /* find the property */
        def = css_properties;
        for(;;) {
            if (def >= css_properties + NB_PROPERTIES) {
                css_error1(b, "unsupported property '%s'", property);
                /* property not found skip it: find next ';' */
                get_str(&p, buf, sizeof(buf), ";");
                goto next;
            }
            if (!strcmp(def->name, property))
                break;
            def++;
        }
        property_index = def - css_properties;
        type = def->type;

        nb_args = 0;
        for(;;) {
            /* get argument */
            skip_spaces(&p);
            if (*p == ';' || *p == '\0')
                break;
            /* more than 1 argument only if wanted */
            if (nb_args >= 1 &&
                !(type & (CSS_TYPE_FOUR|CSS_TYPE_TWO|CSS_TYPE_ARGS)))
                break;
            if (nb_args >= 2 &&
                !(type & (CSS_TYPE_FOUR|CSS_TYPE_ARGS)))
                break;
            if (nb_args >= 4 && 
                (!type & CSS_TYPE_ARGS))
                break;
            if (nb_args >= MAX_ARGS)
                break;

            if (*p == '\"' || *p == '\'') {
                /* string parsing */
                /* if no string expected, continue parsing */
                if (!(type & CSS_TYPE_STRING))
                    goto next;
                args[nb_args].u.str = css_parse_string(&p);
                unit = CSS_VALUE_STRING;
                goto got_val;
            }

            if (type & CSS_TYPE_ATTR) {
                /* attr(x) support */
                if (strstart(p, "attr(", &p)) {
                    skip_spaces(&p);
                    get_str(&p, buf, sizeof(buf), ");");
                    if (buf[0] != '\0') {
                        if (*p != ')')
                            goto next;
                        p++;
                        if (b->ignore_case)
                            css_strtolower(buf, sizeof(buf));
                        args[nb_args].u.attr_id = css_new_ident(buf);
                        unit = CSS_VALUE_ATTR;
                        goto got_val;
                    }
                }
            }

            if (type & CSS_TYPE_COUNTER) {
                /* counter(x[,type]) support */
                if (strstart(p, "counter(", &p)) {
                    skip_spaces(&p);
                    get_str(&p, buf, sizeof(buf), ",);");
                    args[nb_args].u.counter.type = CSS_LIST_STYLE_TYPE_DECIMAL;
                    if (*p == ',') {
                        p++;
                        skip_spaces(&p);
                        get_str(&p, buf2, sizeof(buf2), ");");
                        val = css_get_enum(buf2, list_style_enum);
                        if (val >= 0)
                            args[nb_args].u.counter.type = val;
                    }
                    if (*p != ')')
                        goto next;
                    p++;
                    args[nb_args].u.counter.counter_id = css_new_ident(buf);
                    unit = CSS_VALUE_COUNTER;
                    goto got_val;
                }
            }
            get_str(&p, buf, sizeof(buf), " ;");

            unit = CSS_UNIT_NONE;
            if (type & CSS_TYPE_AUTO) {
                if (!strcmp(buf, "auto")) {
                    val = CSS_AUTO;
                    goto got_val;
                }
            }
            if (!(type & CSS_TYPE_NOINHERIT)) {
                if (!strcmp(buf, "inherit")) {
                    val = CSS_INHERIT;
                    goto got_val;
                }
            }
            if (type & CSS_TYPE_INTEGER) {
                const char *p1;
                val = strtol(buf, (char **)&p1, 0);
                if (*p1 == '\0') {
                    unit = CSS_VALUE_INTEGER;
                    goto got_val;
                }
            }
            if (type & CSS_TYPE_LENGTH) {
                if (!css_get_length(&val, &unit, buf))
                    goto got_val;
            }
            if (type & CSS_TYPE_BORDER_STYLE) {
                val = css_get_enum(buf, border_style_enum);
                if (val >= 0)
                    goto got_val;
            }
            if (type & CSS_TYPE_LIST_STYLE) {
                val = css_get_enum(buf, list_style_enum);
                if (val >= 0)
                    goto got_val;
            }
            if (type & CSS_TYPE_ENUM) {
                val = css_get_enum(buf, def->name + strlen(def->name) + 1);
                if (val >= 0)
                    goto got_val;
            }
            if (type & CSS_TYPE_IDENT) {
                val = css_new_ident(buf);
                unit = CSS_VALUE_IDENT;
                goto got_val;
            }
            if (type & CSS_TYPE_FONT_FAMILY) {
                val = css_get_font_family(buf);
                if (val == 0)
                    val = CSS_INHERIT;
                goto got_val;
            }
            if (type & CSS_TYPE_COLOR) {
                /* XXX: color parsing is not always discriminant */
                if (!css_get_color(&val, buf)) {
                    unit = CSS_VALUE_COLOR;
                    goto got_val;
                }
            }
            css_error1(b, "unrecognized value '%s' for property '%s'", 
                       buf, def->name);
            goto next;
        got_val:
            /* specific handling may be necessary. We do them here */
            switch(property_index) {
            case CSS_font_size:
                if (unit == CSS_UNIT_NONE) {
                    if (val == 7) {
                        /* smaller */
                        unit = CSS_UNIT_PERCENT;
                        val = (CSS_LENGTH_FRAC_BASE * 10) / 12;
                    } else if (val == 8) {
                        /* larger */
                        unit = CSS_UNIT_PERCENT;
                        val = (CSS_LENGTH_FRAC_BASE * 12) / 10;
                    } else if (val >= 0) {
                        unit = CSS_UNIT_IN;
                        val = get_font_size(val);
                    } else {
                    goto next;
                    }
                }
                break;
            case CSS_border:
            case CSS_border_left:
            case CSS_border_top:
            case CSS_border_right:
            case CSS_border_bottom:
                if (unit == CSS_VALUE_COLOR) {
                    property_index1 = property_index + 
                        CSS_border_color - CSS_border;
                } else if (unit == CSS_UNIT_NONE) {
                    property_index1 = property_index + 
                        CSS_border_style - CSS_border;
                } else {
                    property_index1 = property_index + 
                        CSS_border_width - CSS_border;
                }
                args[0].type = unit;
                args[0].u.val = val;
                if (property_index == CSS_border) {
                    for(i=0;i<4;i++) 
                        css_add_prop(&last_prop, property_index1 + 1 + i, 
                                     &args[0]);
                } else {
                        css_add_prop(&last_prop, property_index1, &args[0]);
                }
                /* parse next args without storing them */
                continue;
            }

            args[nb_args].type = unit;
            if (unit != CSS_VALUE_STRING &&
                unit != CSS_VALUE_ATTR &&
                unit != CSS_VALUE_COUNTER) {
                args[nb_args].u.val = val;
            }
            nb_args++;
        }
        if (type & CSS_TYPE_SPECIAL)
            goto next;

        if (type & CSS_TYPE_FOUR) {
            CSSPropertyValue v1, v2, v3, v4;
            /* handle specifically the four args case */
            v1 = args[0];
            switch(nb_args) {
            case 1:
                args[1] = args[2] = args[3] = v1;
                break;
            case 2:
                v2 = args[1];
                args[1] = args[3] = v1;
                args[0] = args[2] = v2;
                break;
            case 3:
                v2 = args[1];
                v3 = args[2];
                args[1] = v1;
                args[0] = args[2] = v2;
                args[3] = v3;
                break;
            case 4:
            default:
                v2 = args[1];
                v3 = args[2];
                v4 = args[3];

                args[1] = v1;
                args[2] = v2;
                args[3] = v3;
                args[0] = v4;
                break;
            }
            for(i=0;i<4;i++) 
                css_add_prop(&last_prop, property_index + 1 + i, &args[i]);
        } else if (type & CSS_TYPE_TWO) {
            if (nb_args == 1)
                args[1] = args[0];
            for(i=0;i<2;i++) 
                css_add_prop(&last_prop, property_index + 1 + i, &args[i]);
        } else if (type & CSS_TYPE_ARGS) {
            /* unbounded number of args */
            css_add_prop_values(&last_prop, property_index, nb_args, args);
        } else {
            css_add_prop(&last_prop, property_index, &args[0]);
        }
    next:
        skip_spaces(&p);
        if (*p != ';')
            break;
        p++;
    }
    return first_prop;
}

#ifdef DEBUG
void css_dump_properties(CSSProperty *prop)
{
    const CSSPropertyDef *def;
    int val, j;
    CSSPropertyValue *value;

    while (prop != NULL) {
        def = css_properties + prop->property;
        val = prop->value.u.val;
        printf("%s: ", def->name);
        if (val == CSS_AUTO) {
            printf("auto");
        } else if (val == CSS_INHERIT) {
            printf("inherit");
        } else {
            value = &prop->value;
            for(j=0;j<prop->nb_values;j++) {
                val = value->u.val;
                switch(value->type) {
                case CSS_UNIT_EX:
                    printf("%0.1fex", (double)val / CSS_LENGTH_FRAC_BASE);
                    break;
                case CSS_UNIT_EM:
                    printf("%0.1fem", (double)val / CSS_LENGTH_FRAC_BASE);
                    break;
                case CSS_UNIT_PERCENT:
                    printf("%0.0f%%", (double)val / CSS_LENGTH_FRAC_BASE);
                    break;
                case CSS_VALUE_COLOR:
                    printf("#%06X", val);
                    break;
                case CSS_UNIT_NONE:
                    {
                        const char *p, *p1;
                        int i, len;
                        char buf[100];
                        
                        if (def->type & CSS_TYPE_BORDER_STYLE)
                            p = border_style_enum;
                        else
                            p = def->name + strlen(def->name) + 1;
                        i = 0;
                        for(;;) {
                            p1 = strchr(p, ',');
                            if (i == val) {
                                if (!p1)
                                    len = strlen(p);
                                else
                                    len = p1 - p;
                                memcpy(buf, p, len);
                                buf[len] = '\0';
                                printf("%s", buf);
                                break;
                            }
                            if (!p1) {
                                printf("[%d]", val);
                                break;
                            }
                            i++;
                            p = p1 + 1;
                        }
                    }
                    break;
                case CSS_VALUE_STRING:
                    printf("\"%s\"", value->u.str);
                    break;
                case CSS_VALUE_ATTR:
                    printf("attr(%s)", css_ident_str(value->u.attr_id));
                    break;
                case CSS_VALUE_COUNTER:
                    printf("counter(%s,%d)", 
                           css_ident_str(value->u.counter.counter_id), 
                           value->u.counter.type);
                    break;
                case CSS_VALUE_INTEGER:
                    printf("%d", val);
                    break;
                case CSS_VALUE_IDENT:
                    printf("%s", css_ident_str(value->u.attr_id));
                    break;
                default:
                    printf("[%d]", val);
                    break;
                }
                if (j != (prop->nb_values - 1))
                    printf(" ");
                value++;
            }
        }
        printf("; ");
        prop = prop->next;
    }
}
#endif

CSSStyleSheet *css_new_style_sheet(void)
{
    CSSStyleSheet *s;

    s = malloc(sizeof(CSSStyleSheet));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->plast_entry = &s->first_entry;
    return s;
}

static void free_selector(CSSSimpleSelector *ss)
{
    CSSStyleSheetAttributeEntry *attr, *attr1;

    for(attr = ss->attrs; attr != NULL; attr = attr1) {
        attr1 = attr->next;
        free(attr);
    }
}

/* XXX: free idents too */
void css_free_style_sheet(CSSStyleSheet *s)
{
    CSSStyleSheetEntry *e, *e1;
    CSSProperty *p, *p1;
    CSSSimpleSelector *ss, *ss1;

    for(e = s->first_entry; e != NULL; e = e1) {
        e1 = e->next;
        
        for(ss = e->sel.next; ss != NULL; ss = ss1) {
            ss1 = ss->next;
            free_selector(ss);
            free(ss);
        }
        free_selector(&e->sel);

        for(p = e->props; p != NULL; p = p1) {
            p1 = p->next;
            free(p);
        }
        free(e);
    }
    
    free(s);
}

static int bgetc1(CSSParseState *b)
{
    int ch;

    ch = (unsigned char)*b->ptr;
    if (ch) {
        b->ptr++;
        return ch;
    } else {
        return EOF;
    }
}

static int bgetc(CSSParseState *b)
{
    int ch;
 redo:
    ch = bgetc1(b);
    if (ch == '/') {
        ch = bgetc1(b);
        if (ch != '*') {
            if (ch != EOF)
                b->ptr--;
            return '/';
        }
        for(;;) {
            ch = bgetc1(b);
            if (ch != '*')
                continue;
            ch = bgetc1(b);
            if (ch == '/')
                break;
        }
        goto redo;
    } else {
        return ch;
    }
}

/* XXX: read_string and read_ident not exactly CSS2 conformant */
static void read_string(CSSParseState *b, int *ch_ptr, char *ident, int ident_size)
{
    int ch, quote;
    char *q;

    quote = *ch_ptr;
    q = ident;
    for(;;) {
        ch = bgetc(b);
        if (ch == quote)
            break;
        if ((q - ident) < ident_size - 1)
            *q++ = ch;
    }
    *q = '\0';
    ch = bgetc(b);
    *ch_ptr = ch;    
}

static void read_ident(CSSParseState *b, int *ch_ptr, char *ident, int ident_size)
{
    char *q;
    int c;

    c = *ch_ptr;
    q = ident;
    for(;;) {
        if (!((c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '*' || c == '_' || c == '-'))
            break;
        if ((q - ident) < ident_size - 1)
            *q++ = c;
        c = bgetc(b);
    }
    *q = '\0';
    *ch_ptr = c;
}

static void bskip_spaces(CSSParseState *b, int *ch_ptr)
{
    int c;

    c = *ch_ptr;
    while (css_is_space(c))
        c = bgetc(b);
    *ch_ptr = c;
}

void add_attribute(CSSStyleSheetAttributeEntry ***last_attr,
                   CSSIdent attr, int op, const char *value)
{
    CSSStyleSheetAttributeEntry *ae;
    
    ae = malloc(sizeof(CSSStyleSheetAttributeEntry) + strlen(value));
    ae->attr = attr;
    ae->op = op;
    strcpy(ae->value, value);
    ae->next = NULL;
    **last_attr = ae;
    *last_attr = &ae->next;
}

/* add the tag in the hash table */
/* XXX: use ident hash table ? */
CSSStyleSheetEntry *add_style_entry(CSSStyleSheet *s,
                                    CSSSimpleSelector *ss,
                                    int media)
{
    CSSStyleSheetEntry *e, **pp;
    
    /* add the style sheet entry */
    e = malloc(sizeof(CSSStyleSheetEntry));
    if (!e)
        return NULL;
    memset(e, 0, sizeof(*e));
    e->sel = *ss;
    e->media = media;
    
    /* add in entry list */
    *s->plast_entry = e;
    s->plast_entry = &e->next;
    e->next = NULL;
    /* add in tag hash table */
    pp = &s->tag_hash[css_hash_ident(e->sel.tag, CSS_TAG_HASH_SIZE)];
    while (*pp != NULL)
        pp = &(*pp)->hash_next;
    *pp = e;
    e->hash_next = NULL;
    return e;
}


/* copy a selector and its associated attributes */
static void dup_selector(CSSSimpleSelector *dest, CSSSimpleSelector *src)
{
    CSSStyleSheetAttributeEntry *attr, *first_attr, **plast_attr;

    first_attr = NULL;
    plast_attr = &first_attr;
    for(attr = src->attrs; attr != NULL; attr = attr->next)
        add_attribute(&plast_attr, attr->attr,
                      attr->op, attr->value);
    memcpy(dest, src, sizeof(CSSSimpleSelector));
    dest->attrs = first_attr;
}

/* duplicate css properties */
static CSSProperty *dup_properties(CSSProperty *props)
{
    CSSProperty *p, *first_p, **plast_p;
    first_p = NULL;
    plast_p = &first_p;
    for(p = props; p != NULL; p = p->next) {
        css_add_prop_values(&plast_p, p->property, p->nb_values, &p->value);
    }
    return first_p;
}

/* merge style sheet 'a' in 's' */
void css_merge_style_sheet(CSSStyleSheet *s, CSSStyleSheet *a)
{
    CSSStyleSheetEntry *e, *e1;
    CSSSimpleSelector *ss, *ss1, ss2, **pss;

    for(e = a->first_entry; e != NULL; e = e->next) {
        /* add selector */
        dup_selector(&ss2, &e->sel);
        e1 = add_style_entry(s, &ss2, e->media);
        
        /* add selector operations */
        pss = &e1->sel.next;
        for(ss = e->sel.next; ss != NULL; ss = ss->next) {
            ss1 = malloc(sizeof(CSSSimpleSelector));
            dup_selector(ss1, ss);
            *pss = ss1;
            pss = &ss1->next;
        }

        /* add css properties */
        e1->props = dup_properties(e->props);
    }
}

static void beat(CSSParseState *b, int *ch_ptr, const char *str)
{
    int ch;

    ch = *ch_ptr;
    while (*str) {
        if (ch == EOF || ch != *str)
            break;
        ch = bgetc(b);
        str++;
    }
    *ch_ptr = ch;
}

static void parse_simple_selector(CSSSimpleSelector *ss, CSSParseState *b,
                                  int *ch_ptr)
{
    char value[1024];
    char tag[64];
    char tag_id[64];
    char attribute[64];
    int ch, pclass, val;
    CSSStyleSheetAttributeEntry *first_attr, **last_attr;

    ch = *ch_ptr;
    
    /* read the tag */
    first_attr = NULL;
    last_attr = &first_attr;
    tag[0] = '\0';
    tag_id[0] = '\0';
    pclass = 0;
    read_ident(b, &ch, tag, sizeof(tag));
    if (b->ignore_case)
        css_strtolower(tag, sizeof(tag));

    /* read '.class', '[xxx]', ':pseudo-class' */
    for(;;) {
        bskip_spaces(b, &ch);
        if (ch == '.') {
            /* read the class and add it as an attribute */
            ch = bgetc(b);
            read_ident(b, &ch, value, sizeof(value));
            add_attribute(&last_attr, CSS_ID_class, CSS_ATTR_OP_EQUAL, value);
        } else if (ch == '#') {
            /* read the id */
            ch = bgetc(b);
            read_ident(b, &ch, tag_id, sizeof(tag_id));
        } else if (ch == '[') {
            /* read the attribute */
            int op;
            ch = bgetc(b);
            read_ident(b, &ch, attribute, sizeof(attribute));
            if (b->ignore_case)
                css_strtolower(attribute, sizeof(attribute));

            switch(ch) {
            case '~':
                op = CSS_ATTR_OP_IN_LIST;
                ch = bgetc(b);
                goto get_value;
            case '=':
                op = CSS_ATTR_OP_EQUAL;
                goto get_value;
            case '|':
                op = CSS_ATTR_OP_IN_HLIST;
                ch = bgetc(b);
            get_value:
                ch = bgetc(b);
                if (ch == '\"' || ch == '\'') {
                    read_string(b, &ch, value, sizeof(value));
                } else {
                    read_ident(b, &ch, value, sizeof(value));
                }
                break;
            case ']':
                op = CSS_ATTR_OP_SET;
                value[0] = '\0';
                break;
            default:
                dprintf("op: incorrect char '%c'\n", ch);
                return; /* cannot do more */
            }
            if (ch == ']')
                ch = bgetc(b);
            add_attribute(&last_attr, css_new_ident(attribute), op, value);
        } else if (ch == ':') {
            ch = bgetc(b);
            read_ident(b, &ch, value, sizeof(value));
            val = css_get_enum(value, "first-child,link,visited,active,hover,focus,first-line,first-letter,before,after");
            if (val >= 0)
                pclass |= 1 << val;
        } else {
            break;
        }
    }
    memset(ss, 0, sizeof(CSSSimpleSelector));
    if (tag[0] == '\0') {
        ss->tag = CSS_ID_ALL;
    } else {
        ss->tag = css_new_ident(tag);
    }
    if (tag_id[0] != '\0') {
        /* XXX: not fully correct, but good enough for now */
        add_attribute(&last_attr, CSS_ID_id, CSS_ATTR_OP_EQUAL, value);
        /* we also add the id, just in case we use it in the futur */
        ss->tag_id = css_new_ident(tag_id);
    }
    ss->attrs = first_attr;
    ss->pclasses = pclass;

    *ch_ptr = ch;
}


/* flags: only XML_IGNORE_CASE is handled */
void css_parse_style_sheet(CSSStyleSheet *s, CSSParseState *b)
{
    char value[1024];
    char tag[64];
    char tag_id[64];
    char *q;
    int ch, media, val, last_tree_op, i;
    CSSStyleSheetEntry *e, **first_eprops;
    CSSSimpleSelector ss2, *ss = &ss2, *last_ss, *ss1;
    CSSProperty *props;
    
    ch = bgetc(b);
    media = CSS_MEDIA_ALL;
    for(;;) {
    redo:
        first_eprops = s->plast_entry;
        bskip_spaces(b, &ch);
        if (ch == EOF)
            break;
        /* eat inserted HTML comments for compatible STYLE tag parsing */
        if (ch == '<') {
            beat(b, &ch, "<!--");
            goto redo;
        } else if (ch == '-') {
            beat(b, &ch, "-->");
            goto redo;
        }

        /* handle '@media { ... }' */
        if (ch == '@') {
            ch = bgetc(b);
            read_ident(b, &ch, tag, sizeof(tag));
            switch(css_get_enum(tag, "media,page")) {
            case 0:
                /* @media */
                media = 0;
                for(;;) {
                    bskip_spaces(b, &ch);
                    read_ident(b, &ch, tag, sizeof(tag));
                    val = css_get_enum(tag, "tty,screen,print,tv,speech,all");
                    if (val < 0 || val == 5) 
                        media = CSS_MEDIA_ALL;
                    else
                        media |= (1 << val);
                    bskip_spaces(b, &ch);
                    if (ch == ',') {
                        ch = bgetc(b);
                    } else if (ch == '{' || ch == EOF) {
                        ch = bgetc(b);
                        break;
                    }
                }
                goto redo;
            case 1:
                /* @page */
                bskip_spaces(b, &ch);
                if (ch != '{') {
                    read_ident(b, &ch, tag_id, sizeof(tag_id));
                    bskip_spaces(b, &ch);
                }
                memset(ss, 0, sizeof(CSSSimpleSelector));
                ss->tag = css_new_ident("@page");
                if (tag_id[0] != '\0')
                    ss->tag_id = css_new_ident(tag_id);
                add_style_entry(s, ss, media);
                goto parse_props;
            default:
                css_error1(b, "unrecognized css directive '@%s'", tag);
                break;
            }
        } else if (ch == '}') {
            /* XXX: end of media, should unstack */
            ch = bgetc(b);
            goto redo;
        }
                
        /* parse a selector list */
        for(;;) {
            /* parse simple selectors with operations */
            last_ss = NULL;
            last_tree_op = CSS_TREE_OP_NONE;
            for(;;) {
                int tree_op;
                bskip_spaces(b, &ch);
                parse_simple_selector(ss, b, &ch);
                bskip_spaces(b, &ch);
                ss->tree_op = last_tree_op;
                ss->next = last_ss;
                if (ch == '+') {
                    tree_op = CSS_TREE_OP_PRECEEDED;
                    ch = bgetc(b);
                    goto add_tree;
                } else if (ch == '>') {
                    tree_op = CSS_TREE_OP_CHILD;
                    ch = bgetc(b);
                    goto add_tree;
                } else if (isalpha(ch)) {
                    tree_op = CSS_TREE_OP_DESCENDANT;
                add_tree:
                    ss1 = malloc(sizeof(CSSSimpleSelector));
                    if (ss1) {
                        memcpy(ss1, ss, sizeof(CSSSimpleSelector));
                        last_ss = ss1;
                    }
                    last_tree_op = tree_op;
                } else {
                    /* other char: exit */
                    break;
                }
            }
            add_style_entry(s, ss, media);

            /* get next selector, if present */
            if (ch != ',')
                break;
            ch = bgetc(b);
        }
    parse_props:
        /* expect start of properties */
        if (ch != '{')
            break;
        ch = bgetc(b);

        q = value;
        while (ch != '}' && ch != EOF) {
            if ((q - value) < sizeof(value) - 1)
                *q++ = ch;
            ch = bgetc(b);
        }
        *q = '\0';
        
        if (ch == '}')
            ch = bgetc(b);

        /* the properties are extracted, now add them to each tag */
        /* XXX: should locate entries first, then add, to avoid adding
           duplicate entries */
        /* XXX: should put font properties first to avoid em/ex units
           problems, but it would still not be sufficient. */
        props = css_parse_properties(b, value);
        i = 0;
        for(e = *first_eprops; e != NULL; e = e->next) {
            if (i == 0)
                e->props = props;
            else
                e->props = dup_properties(props);
            i++;
        }
    }
#ifdef DEBUG
    css_dump_style_sheet(s);
#endif
}

void css_parse_style_sheet_str(CSSStyleSheet *s, const char *buffer, int flags)
{
    CSSParseState b1, *b = &b1;
    b->ptr = buffer;
    b->filename = "builtin";
    b->line_num = 1;
    b->ignore_case = flags & XML_IGNORE_CASE;
    css_parse_style_sheet(s, b);
}

#ifdef DEBUG
static void dump_selector(CSSSimpleSelector *ss)
{
    CSSStyleSheetAttributeEntry *ae;

    switch(ss->tree_op) {
    case CSS_TREE_OP_DESCENDANT:
        dump_selector(ss->next);
        printf(" ");
        break;
    case CSS_TREE_OP_CHILD:
        dump_selector(ss->next);
        printf(" > ");
        break;
    case CSS_TREE_OP_PRECEEDED:
        dump_selector(ss->next);
        printf(" + ");
        break;
    default:
        break;
    }

    printf("%s", css_ident_str(ss->tag));
    if (ss->pclasses & CSS_PCLASS_FIRST_CHILD)
        printf(":first-child");

    if (ss->tag_id)
        printf("#%s", css_ident_str(ss->tag_id));
    ae = ss->attrs;
    while (ae != NULL) {
        printf("[%s", css_ident_str(ae->attr));
        switch(ae->op) {
        case CSS_ATTR_OP_EQUAL:
            printf("=%s", ae->value);
            break;
        case CSS_ATTR_OP_IN_LIST:
            printf("~=%s", ae->value);
                break;
        case CSS_ATTR_OP_IN_HLIST:
            printf("|=%s", ae->value);
            break;
        }
        printf("]");
        ae = ae->next;
    }
}

void css_dump_style_sheet(CSSStyleSheet *s)
{
    CSSStyleSheetEntry *e;

    printf("<STYLE type=\"text/css\">\n");
    e = s->first_entry;
    while (e != NULL) {

        dump_selector(&e->sel);
        printf(" { ");
        css_dump_properties(e->props);
        printf("}\n");
        e = e->next;
    }
    printf("</STYLE>\n");
}
#endif


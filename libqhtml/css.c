/*
 * CSS core for qemacs.
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

static void css_counter_str(char *text, int text_size, 
                            int index, int list_style_type, int adjust);

#define CSSDEF(str, ident, inherited, type) \
  { str, (int)&((CSSState *)0)->ident, CSS_STORAGE_INT, \
    type | (inherited ? CSS_TYPE_INHERITED : 0) },

#define CSSDEF1(str, ident, inherited, type, storage) \
  { str, (int)&((CSSState *)0)->ident, storage, \
    type | (inherited ? CSS_TYPE_INHERITED : 0) },
#define CSSUNDEF(str)

const CSSPropertyDef css_properties[NB_PROPERTIES] = {
    CSSDEF("display\0inline,block,table,table-row,table-row-group,table-header-group,table-footer-group,table-column,table-column-group,table-cell,table-caption,list-item,marker,inline-block,inline-table,none", 
           display, 0, CSS_TYPE_ENUM)
    CSSDEF("color", color, 1, CSS_TYPE_COLOR)
    CSSDEF("background-color", bgcolor, 0, CSS_TYPE_COLOR)
    CSSDEF("white-space\0normal,pre,nowrap,prewrap", 
           white_space, 1, CSS_TYPE_ENUM)
    CSSDEF("direction\0ltr,rtl", direction, 1, CSS_TYPE_ENUM)
    CSSDEF("float\0none,left,right", block_float, 0, CSS_TYPE_ENUM)
    CSSDEF("font-family", font_family, 1, CSS_TYPE_FONT_FAMILY)
    CSSDEF("font-style\0normal,italic,oblique", font_style, 1, CSS_TYPE_ENUM)
    CSSDEF("font-weight\0normal,bold,bolder,lighter", font_weight, 1, CSS_TYPE_ENUM)
    CSSDEF("font-size\0xx-small,x-small,small,medium,large,x-large,xx-large,smaller,larger", font_size, 1, CSS_TYPE_LENGTH | CSS_TYPE_ENUM)
    CSSDEF("text-decoration\0none,underline,line-through", text_decoration, 1, CSS_TYPE_ENUM)
    CSSDEF("text-align\0left,right,center,justify", text_align, 1, CSS_TYPE_ENUM)
    CSSDEF("width", width, 0, CSS_TYPE_LENGTH | CSS_TYPE_AUTO)
    CSSDEF("height", height, 0, CSS_TYPE_LENGTH | CSS_TYPE_AUTO)
    CSSDEF("unicode-bidi\0normal,embed,bidi-override", unicode_bidi, 0, CSS_TYPE_ENUM)

    CSSDEF("border-width", border, 0, CSS_TYPE_LENGTH | CSS_TYPE_FOUR)
    CSSDEF("border-left-width", border.x1, 0, CSS_TYPE_LENGTH)
    CSSDEF("border-top-width", border.y1, 0, CSS_TYPE_LENGTH)
    CSSDEF("border-right-width", border.x2, 0, CSS_TYPE_LENGTH)
    CSSDEF("border-bottom-width", border.y2, 0, CSS_TYPE_LENGTH)

    CSSDEF("border-color", border_colors, 0, CSS_TYPE_COLOR | CSS_TYPE_FOUR)
    CSSDEF("border-left-color", border_colors[0], 0, CSS_TYPE_COLOR)
    CSSDEF("border-top-color", border_colors[1], 0, CSS_TYPE_COLOR)
    CSSDEF("border-right-color", border_colors[2], 0, CSS_TYPE_COLOR)
    CSSDEF("border-bottom-color", border_colors[3], 0, CSS_TYPE_COLOR)

    CSSDEF("border-style", border_styles, 0, CSS_TYPE_BORDER_STYLE | CSS_TYPE_FOUR)
    CSSDEF("border-left-style", border_styles[0], 0, CSS_TYPE_BORDER_STYLE)
    CSSDEF("border-top-style", border_styles[1], 0, CSS_TYPE_BORDER_STYLE)
    CSSDEF("border-right-style", border_styles[2], 0, CSS_TYPE_BORDER_STYLE)
    CSSDEF("border-bottom-style", border_styles[3], 0, CSS_TYPE_BORDER_STYLE)

    CSSDEF("border", border, 0, CSS_TYPE_LENGTH | CSS_TYPE_COLOR | CSS_TYPE_BORDER_STYLE | CSS_TYPE_SPECIAL)
    CSSDEF("border-left", border, 0, CSS_TYPE_LENGTH | CSS_TYPE_COLOR | CSS_TYPE_BORDER_STYLE | CSS_TYPE_SPECIAL)
    CSSDEF("border-top", border, 0, CSS_TYPE_LENGTH | CSS_TYPE_COLOR | CSS_TYPE_BORDER_STYLE | CSS_TYPE_SPECIAL)
    CSSDEF("border-right", border, 0, CSS_TYPE_LENGTH | CSS_TYPE_COLOR | CSS_TYPE_BORDER_STYLE | CSS_TYPE_SPECIAL)
    CSSDEF("border-bottom", border, 0, CSS_TYPE_LENGTH | CSS_TYPE_COLOR | CSS_TYPE_BORDER_STYLE | CSS_TYPE_SPECIAL)

    CSSDEF("padding", padding, 0, CSS_TYPE_LENGTH | CSS_TYPE_FOUR)
    CSSDEF("padding-left", padding.x1, 0, CSS_TYPE_LENGTH)
    CSSDEF("padding-top", padding.y1, 0, CSS_TYPE_LENGTH)
    CSSDEF("padding-right", padding.x2, 0, CSS_TYPE_LENGTH)
    CSSDEF("padding-bottom", padding.y2, 0, CSS_TYPE_LENGTH)
    
    CSSDEF("margin", margin, 0, CSS_TYPE_LENGTH | CSS_TYPE_FOUR)
    CSSDEF("margin-left", margin.x1, 0, CSS_TYPE_LENGTH)
    CSSDEF("margin-top", margin.y1, 0, CSS_TYPE_LENGTH)
    CSSDEF("margin-right", margin.x2, 0, CSS_TYPE_LENGTH)
    CSSDEF("margin-bottom", margin.y2, 0, CSS_TYPE_LENGTH)
    CSSDEF("clear\0none,left,right,both", clear, 0, CSS_TYPE_ENUM)
    CSSDEF("overflow\0visible,hidden", overflow, 0, CSS_TYPE_ENUM)
    CSSDEF("visibility\0visible,hidden", visibility, 0, CSS_TYPE_ENUM)
    CSSDEF("table-layout\0auto,fixed", table_layout, 0, CSS_TYPE_ENUM)
    CSSDEF("vertical-align\0baseline,sub,super,top,text-top,middle,bottom,text-bottom", vertical_align, 0, CSS_TYPE_ENUM)
    CSSDEF("border-collapse\0collapse,separate", border_collapse, 1, CSS_TYPE_ENUM)
    CSSDEF("border-spacing", border_spacing_horizontal, 1, CSS_TYPE_LENGTH | CSS_TYPE_TWO)
    CSSDEF("border-spacing-horizontal", border_spacing_horizontal, 1, CSS_TYPE_LENGTH)
    CSSDEF("border-spacing-vertical", border_spacing_vertical, 1, CSS_TYPE_LENGTH)
    /* XXX: missing types */
    CSSDEF("line-height", line_height, 1, CSS_TYPE_LENGTH | CSS_TYPE_AUTO)
    CSSDEF("position\0static,relative,absolute,fixed", position, 0, CSS_TYPE_ENUM)
    CSSDEF1("content", content, 0, CSS_TYPE_STRING | CSS_TYPE_ATTR | 
            CSS_TYPE_COUNTER | CSS_TYPE_ARGS, CSS_STORAGE_PTR)
    CSSDEF("caption-side\0top,bottom,left,right", caption_side, 1, CSS_TYPE_ENUM)
    CSSDEF("marker-offset", marker_offset, 0, CSS_TYPE_LENGTH | CSS_TYPE_AUTO)
    CSSDEF("list-style-type", list_style_type, 1, CSS_TYPE_LIST_STYLE)
    CSSDEF("column-span", column_span, 0, CSS_TYPE_INTEGER)
    CSSDEF("row-span", row_span, 0, CSS_TYPE_INTEGER)
    CSSDEF1("content-alt", content_alt, 0, CSS_TYPE_STRING, CSS_STORAGE_PTR)
    CSSDEF("list-style-position\0outside,inside", list_style_position, 1, CSS_TYPE_ENUM)
    CSSDEF1("counter-reset\0none", counter_reset, 0, CSS_TYPE_ENUM | CSS_TYPE_INTEGER | CSS_TYPE_IDENT | CSS_TYPE_ARGS, CSS_STORAGE_PTR)
    CSSDEF1("counter-increment\0none", counter_increment, 0, CSS_TYPE_ENUM | CSS_TYPE_INTEGER | CSS_TYPE_IDENT | CSS_TYPE_ARGS, CSS_STORAGE_PTR)
    CSSDEF("bidi-mode\0normal,test", bidi_mode, 1, CSS_TYPE_ENUM)
    CSSDEF("left", left, 0, CSS_TYPE_LENGTH | CSS_TYPE_AUTO)
    CSSDEF("top", top, 0, CSS_TYPE_LENGTH | CSS_TYPE_AUTO)
    CSSDEF("right", right, 0, CSS_TYPE_LENGTH | CSS_TYPE_AUTO)
    CSSDEF("bottom", bottom, 0, CSS_TYPE_LENGTH | CSS_TYPE_AUTO)

    /* unsupported properties. We list them to ease documentation generation */
    CSSUNDEF("background")
    CSSUNDEF("background-attachment")
    CSSUNDEF("background-image")
    CSSUNDEF("background-position")
    CSSUNDEF("background-repeat")
    CSSUNDEF("clip")
    CSSUNDEF("cursor")
    CSSUNDEF("empty-cells")
    CSSUNDEF("font")
    CSSUNDEF("font-size-adjust")
    CSSUNDEF("font-stretch")
    CSSUNDEF("font-variant")
    CSSUNDEF("letter-spacing")
    CSSUNDEF("list-style")
    CSSUNDEF("list-style-image")
    CSSUNDEF("max-height")
    CSSUNDEF("max-width")
    CSSUNDEF("min-height")
    CSSUNDEF("min-width")
    CSSUNDEF("outline")
    CSSUNDEF("outline-color")
    CSSUNDEF("outline-style")
    CSSUNDEF("outline-width")
    CSSUNDEF("quotes")
    CSSUNDEF("text-indent")
    CSSUNDEF("text-shadow")
    CSSUNDEF("text-transform")
    CSSUNDEF("word-spacing")
    CSSUNDEF("z-index")
    
    /* paged media */
    CSSUNDEF("marks")
    CSSUNDEF("page")
    CSSUNDEF("page-break-after")
    CSSUNDEF("page-break-before")
    CSSUNDEF("page-break-inside")
    CSSUNDEF("size")
    CSSUNDEF("orphans")
    CSSUNDEF("widows")

    /* all aural properties are unsupported */
    CSSUNDEF("azimuth")
    CSSUNDEF("cue")
    CSSUNDEF("cue-after")
    CSSUNDEF("cue-before")
    CSSUNDEF("elevation")
    CSSUNDEF("pause")
    CSSUNDEF("pause-after")
    CSSUNDEF("pause-before")
    CSSUNDEF("pitch")
    CSSUNDEF("pitch-range")
    CSSUNDEF("pitch-during")
    CSSUNDEF("richness")
    CSSUNDEF("speak")
    CSSUNDEF("speak-header")
    CSSUNDEF("speak-punctuation")
    CSSUNDEF("speak-rate")
    CSSUNDEF("stress")
    CSSUNDEF("voice-family")
    CSSUNDEF("volume")

};

/*************************************************/

/* the following will be moved to other files */

/* XXX: use unicode */
typedef int (*NextCharFunc)(EditBuffer *b, unsigned long *offset);

int eb_nextc1(EditBuffer *b, unsigned long *offset_ptr)
{
    unsigned long offset1;
    int ch, ch1;
    char name[16], *q;

    ch = eb_nextc(b, *offset_ptr, (int *)offset_ptr);
    if (ch == '&') {
        /* read entity */
        offset1 = *offset_ptr;
        q = name;
        for(;;) {
            ch1 = eb_nextc(b, *offset_ptr, (int *)offset_ptr);
            if (ch1 == '\n' || ch1 == ';')
                break;
            *q++ = ch1;
            if (q >= name + sizeof(name) - 1)
                break;
        }
        *q = '\0';
        ch1 = find_entity(name);
        if (ch1 >= 0) {
            ch = ch1;
        } else {
            /* entity not found: roll back */
            *offset_ptr = offset1;
        }
    }
    return ch;
}

int str_nextc(EditBuffer *b, unsigned long *offset_ptr)
{
    const unsigned char *ptr;
    int ch;

    ptr = *(const unsigned char **)offset_ptr;
    ch = *ptr;
    if (ch >= 128) {
        ch = utf8_decode((const char **)&ptr);
    } else {
        ptr++;
    }
    *(const unsigned char **)offset_ptr = ptr;
    return ch;
}

static NextCharFunc get_nextc(CSSBox *box)
{
    switch(box->content_type) {
    case CSS_CONTENT_TYPE_STRING:
        return str_nextc;
    case CSS_CONTENT_TYPE_BUFFER:
        return eb_nextc1;
    default:
        assert(1);
        return NULL;
    }
}

/***********************************************************/
/* CSS identifier manager */

#define CSS_IDENT_HASH_SIZE 521
#define CSS_IDENT_INCR     128

static CSSIdentEntry *hash_ident[CSS_IDENT_HASH_SIZE];
static CSSIdentEntry **table_ident;
static int table_ident_nb, table_ident_allocated;

const char css_idents[] = 
"\0"
"*\0"
#define CSSID(id) #id "\0"
#define CSSIDSTR(id, str) str "\0"
#include "cssid.h"
#undef CSSID
#undef CSSIDSTR
"\1";

static unsigned int hash_str(const char *str, unsigned int hash_size)
{
    unsigned int h, ch;
    h = 1;
    while (*str) {
        ch = *(unsigned char *)str++;
        h = ((h << 8) + ch) % hash_size;
    }
    return h;
}

const char *css_ident_str(CSSIdent id)
{
    return table_ident[id]->str;
}

CSSIdent css_new_ident(const char *str)
{
    CSSIdentEntry **pp, *p;
    int n;

    pp = &hash_ident[hash_str(str, CSS_IDENT_HASH_SIZE)];
    p = *pp;
    while (p != NULL) {
        if (!strcmp(str, p->str)) {
            return p->id;
        }
        p = p->hash_next;
    }
    p = malloc(sizeof(CSSIdentEntry) + strlen(str));
    if (!p)
        return CSS_ID_NIL;
    p->id = table_ident_nb;
    strcpy(p->str, str);
    p->hash_next = *pp;
    *pp = p;

    /* put ident in table */
    if (table_ident_nb == table_ident_allocated) {
        n = table_ident_allocated + CSS_IDENT_INCR;
        pp = realloc(table_ident, n * sizeof(CSSIdentEntry *));
        if (!pp) {
            free(p);
            return CSS_ID_NIL;
        }
        table_ident = pp;
        table_ident_allocated = n;
    }
    table_ident[table_ident_nb++] = p;

    return p->id;
}

static void css_init_idents(void)
{
    const char *p, *r;
    char buf[64];

    if (table_ident_nb != 0)
        return;
    
    for(p = css_idents; *p != '\1'; p = r + 1) {
        r = p;
        while (*r)
            r++;
        memcpy(buf, p, r - p);
        buf[r - p] = '\0';
        css_new_ident(buf);
    }
}

/****************************************************/

/* set a counter in the current element to value "value". If not
   present, then add it */
static void set_counter(CSSContext *s, CSSIdent counter_id, int value)
{
    CSSCounterValue *p;
    for(p = s->counter_stack_ptr; p != s->counter_stack_base; p = p->prev) {
        if (p->counter_id == counter_id) {
            p->value = value;
        }
    }
    p = malloc(sizeof(CSSCounterValue));
    if (!p)
        return;
    p->counter_id = counter_id;
    p->value = value;
    p->prev = s->counter_stack_ptr;
    s->counter_stack_ptr = p;
}

/* save counter stack so that we can free counters later */
static CSSCounterValue *push_counters(CSSContext *s)
{
    CSSCounterValue *p;
    p = s->counter_stack_base;
    s->counter_stack_base = s->counter_stack_ptr;
    return p;
}

/* pop counters until counter_stack_ptr reaches 'counter_stack_base' */
static void pop_counters(CSSContext *s, CSSCounterValue *p2)
{
    CSSCounterValue *p, *p1;

    for(p = s->counter_stack_ptr; p != s->counter_stack_base; p = p1) {
        p1 = p->prev;
        free(p);
        p = p1;
    }
    s->counter_stack_ptr = s->counter_stack_base;
    s->counter_stack_base = p2;
}

static void incr_counter(CSSContext *s, CSSIdent counter_id, int incr)
{
    CSSCounterValue *p;
    for(p = s->counter_stack_ptr; p != NULL; p = p->prev) {
        if (p->counter_id == counter_id) {
            p->value += incr;
            return;
        }
    }
    set_counter(s, counter_id, incr);
}

/* return the value of a given counter, and zero if it is not defined */
static int get_counter(CSSContext *s, CSSIdent counter_id)
{
    CSSCounterValue *p;
    for(p = s->counter_stack_ptr; p != NULL; p = p->prev) {
        if (p->counter_id == counter_id) {
            return p->value;
        }
    }
    return 0; /* default value is zero */
}

static QEFont *css_select_font(QEditScreen *screen, CSSState *props);

static void css_eval_property(CSSContext *s,
                              CSSState *state, 
                              CSSProperty *p, 
                              CSSState *state_parent,
                              CSSBox *box)
{
    int *ptr, val;
    const CSSPropertyDef *def;

    if (p->property >= NB_PROPERTIES) {
        dprintf("css_eval: invalid property: %d\n", p->property);
        return;
    }
    def = &css_properties[p->property];
    ptr = (int *)((char *)state + def->struct_offset);
    if (p->value.u.val == CSS_INHERIT) {
        /* XXX: invalid if pointer */
        switch(def->storage) {
        default:
        case CSS_STORAGE_INT:
            *ptr = *(int *)((char *)state_parent + def->struct_offset);
            break;
        case CSS_STORAGE_PTR:
            *(void **)ptr = *(void **)((char *)state_parent + def->struct_offset);
            break;
        }
    } else {
        if (def->storage == CSS_STORAGE_PTR) {
            *(void **)ptr = p;
        } else {
            switch(p->value.type) {
            case CSS_VALUE_COLOR:
            case CSS_UNIT_NONE:
            case CSS_VALUE_INTEGER:
                *ptr = p->value.u.val;
                break;
            case CSS_UNIT_PIXEL:
                /* convert from px to display pixels */
                *ptr = (p->value.u.val * s->px_size) >> CSS_LENGTH_FRAC_BITS;
                break;
            case CSS_UNIT_IN:
                /* convert from inches to display pixels */
                *ptr = (p->value.u.val * s->dots_per_inch) >> CSS_LENGTH_FRAC_BITS;
                break;
            case CSS_UNIT_PERCENT:
                val = *(int *)((char *)state_parent + def->struct_offset);
                *ptr = (p->value.u.val * val) >> CSS_LENGTH_FRAC_BITS;
                break;
            case CSS_UNIT_EX:
                /* currently, we do not use the font metrics */
                val = (state->font_size * CSS_EX_SCALE) >> 
                    CSS_LENGTH_FRAC_BITS;
                *ptr = (p->value.u.val * val) >> CSS_LENGTH_FRAC_BITS;
                break;
            case CSS_UNIT_EM:
                /* special case for font size : inherit from parent */
                if (p->property == CSS_font_size)
                    val = state_parent->font_size;
                else
                    val = state->font_size;
                *ptr = (p->value.u.val * val) >> CSS_LENGTH_FRAC_BITS;
                break;
            }
        }
    }
}

static inline int attribute_match(CSSStyleSheetAttributeEntry *e,
                                  CSSAttribute *a)
{
    CSSIdent attr;
    int op;

    /* no match possible if no attribute */
    if (!a)
        return 0;
    attr = e->attr;
    op = e->op;
    do {
        if (attr == a->attr) {
            switch(op) {
            case CSS_ATTR_OP_SET:
                return 1;
            case CSS_ATTR_OP_EQUAL:
                /* XXX: use global case setting, as in parsing */
                if (!strcasecmp(e->value, a->value))
                    return 1;
                break;
            case CSS_ATTR_OP_IN_LIST:
            case CSS_ATTR_OP_IN_HLIST:
                /* XXX: todo */
                break;
            }
        }
        a = a->next;
    } while (a != NULL);
    return 0;
}

/* return true if (box,pelement) matches simple selector ss (may
   recurse) */
/* XXX: exclude anonymous boxes */
static int selector_match(CSSSimpleSelector *ss, 
                          CSSBox *box)
{
    CSSStyleSheetAttributeEntry *ae;
    CSSBox *box1, *lbox;

    if (ss->tag != box->tag && ss->tag != CSS_ID_ALL)
        return 0;
    
    /* first child test */
    if (ss->pclasses & CSS_PCLASS_FIRST_CHILD) {
        box1 = box->parent;
        if (box1 && box != box1->u.child.first)
            return 0;
    }

    switch(ss->tree_op) {
    case CSS_TREE_OP_DESCENDANT:
        box1 = box->parent;
        while (box1 != NULL) {
            if (selector_match(ss->next, box1))
                goto found;
            box1 = box1->parent;
        }
        return 0;
    case CSS_TREE_OP_CHILD:
        box1 = box->parent;
        if (box1 && selector_match(ss->next, box1))
            goto found;
        return 0;
    case CSS_TREE_OP_PRECEEDED:
        /* search previous box */
        box1 = box->parent;
        if (box1 && box1->content_type == CSS_CONTENT_TYPE_CHILDS) {
            box1 = box1->u.child.first;
            lbox = NULL;
            while (box1 != NULL && box1 != box) {
                lbox = box1;
                box1 = box1->next;
            }
            if (lbox && selector_match(ss->next, lbox)) 
                goto found;
        }
        return 0;
    default:
        break; /* match */
    }
found:
    /* verify that attributes match */
    ae = ss->attrs;
    while (ae != NULL) {
        if (!attribute_match(ae, box->attrs))
            return 0;
        ae = ae->next;
    }
    return 1;
}

#define PELEMENTS_MASK (~CSS_PCLASS_FIRST_CHILD)

static int apply_properties(CSSContext *s, CSSIdent tag, 
                            CSSBox *box, int pelement,
                            CSSState *state,
                            CSSState *state_parent)
{
    CSSStyleSheetEntry *e;
    CSSProperty *p;
    int pelement_found;
    
    /* go thru the style sheet */
    e = s->style_sheet->tag_hash[css_hash_ident(tag, CSS_TAG_HASH_SIZE)];
    pelement_found = 0;
    while (e != NULL) {
        if (e->sel.tag == tag) {
            /* verify media */
            if ((s->media & e->media) == 0)
                goto next;
            if (selector_match(&e->sel, box)) {
                /* add pseudo classes found */
                pelement_found |= e->sel.pclasses;
                /* see if selector pseudo classes matches pseudo
                   element (null means none) */
                if ((pelement == 0 && 
                     (e->sel.pclasses & PELEMENTS_MASK) == 0) ||
                    (pelement != 0 && 
                     (e->sel.pclasses & pelement) != 0)) {
                    /* apply properties */
                    p = e->props;
                    while (p != NULL) {
                        css_eval_property(s, state, p, state_parent, box);
                        p = p->next;
                    }
                }
            }
        }
    next:
        e = e->hash_next;
    }
    return pelement_found;
}

static char *eval_content(CSSContext *s, CSSProperty *p, CSSBox *box)
{
    CSSPropertyValue *value;
    CSSAttribute *attr;
    char buf[4096]; /* XXX: make it dynamic ? */
    char buf1[256];
    int i, index;

    buf[0] = '\0';
    value = &p->value;
    for(i=0;i<p->nb_values;i++) {
        switch(value->type) {
        case CSS_VALUE_STRING:
            pstrcat(buf, sizeof(buf), value->u.str);
            break;
        case CSS_VALUE_ATTR:
            for(attr = box->attrs; attr != NULL; attr = attr->next) {
                if (attr->attr == value->u.attr_id) {
                    pstrcat(buf, sizeof(buf), attr->value);
                    break;
                }
            }
            break;
        case CSS_VALUE_COUNTER:
            index = get_counter(s, value->u.counter.counter_id);
            css_counter_str(buf1, sizeof(buf1), index, 
                            value->u.counter.type, 0);
            pstrcat(buf, sizeof(buf), buf1);
            break;
        }
        value++;
    }
    if (buf[0] != '\0')
        return strdup(buf);
    else
        return NULL;
}

static void eval_counter_update(CSSContext *s, CSSProperty *p)
{
    CSSPropertyValue *value = &p->value;
    int counter_id, n, i;
    
    for(i=0;i<p->nb_values;) {
        if (value->type != CSS_VALUE_IDENT)
            break;
        counter_id = value->u.attr_id;
        i++;
        value++;
        if (p->property == CSS_counter_reset)
            n = 0;
        else
            n = 1;
        /* optional init value */
        if (i < p->nb_values && value->type == CSS_VALUE_INTEGER) {
            n = value->u.val;
            i++;
            value++;
        }
        if (p->property == CSS_counter_reset)
            set_counter(s, counter_id, n);
        else
            incr_counter(s, counter_id, n);
    }
}

/* only rules matching (box, pelement) are considered. All the pseudo
   element found for the box are returned, so that new boxes can be
   created for before & after pseudo elements. */
static int css_eval(CSSContext *s,
                    CSSState *state, CSSBox *box, int pelement, 
                    CSSState *state_parent)
{
    const CSSPropertyDef *def;
    int *ptr_parent, *ptr, type, val, i;
    CSSProperty *p;
    int pelement_found;
    
    /* inherit properties or set to default value */
    def = css_properties;
    while (def < css_properties + NB_PROPERTIES) {
        type = def->type;
        /* the TYPE_FOUR are not real css properties */
        if (!(type & CSS_TYPE_FOUR)) {
            ptr = (int *)((char *)state + def->struct_offset);
            if (def->type & CSS_TYPE_INHERITED) {
                /* inherit value */
                ptr_parent = (int *)((char *)state_parent + def->struct_offset);
                val = *ptr_parent;
            } else {
                /* default values: color assumed to transparent, and
                   if auto is a possible value, then it is
                   set. Otherwise zero is the value */
                if (type & CSS_TYPE_COLOR) {
                    val = COLOR_TRANSPARENT;
                } else if (type & CSS_TYPE_AUTO) {
                    val = CSS_AUTO;
                } else {
                    val = 0;
                }
            }
            *ptr = val;
        }
        def++;
    }

    /* apply generic attributes */
    pelement_found = apply_properties(s, CSS_ID_ALL, box, pelement,
                                      state, state_parent);
    if (box->tag) {
        pelement_found |= apply_properties(s, box->tag, box, pelement,
                                           state, state_parent);
    }

    /* apply explicit properties */
    p = box->properties;
    while (p != NULL) {
        css_eval_property(s, state, p, state_parent, box);
        p = p->next;
    }
    
    /* first reset counters */
    if (state->counter_reset)
        eval_counter_update(s, state->counter_reset);
    /* then increment */
    if (state->counter_increment)
        eval_counter_update(s, state->counter_increment);
    /* alternate content if image (need more ideas) */
    if (state->content_alt && 
        box->content_type == CSS_CONTENT_TYPE_IMAGE) {
        box->u.image.content_alt = eval_content(s, state->content_alt, box);
    }

    /* border colors are set to color by default */
    for(i=0;i<4;i++) {
        if (state->border_colors[i] == COLOR_TRANSPARENT)
            state->border_colors[i] = state->color;
    }
    return pelement_found;
}

static void set_default_props(CSSContext *s, CSSState *props)
{
    memset(props, 0, sizeof(CSSState));
    
    /* size of 12 points */
    props->font_size = (12 * s->dots_per_inch) / 72;
    props->font_family = QE_FAMILY_SERIF;
    props->border_colors[0] = COLOR_TRANSPARENT;
    props->border_colors[1] = COLOR_TRANSPARENT;
    props->border_colors[2] = COLOR_TRANSPARENT;
    props->border_colors[3] = COLOR_TRANSPARENT;
    props->bgcolor = COLOR_TRANSPARENT;
    props->line_height = CSS_AUTO;
}

#if 0
/* accepts null strings */
static inline int strcmp_null(const char *s1, const char *s2)
{
    if (s1 == NULL || s2 == NULL) {
        return (s1 != s2);
    } else {
        return strcmp(s1, s2);
    }
}
#endif

/* hash a byte string */
/* XXX: faster hash ? */
static inline unsigned int hash_bytes(unsigned int h, 
                                      const unsigned char *p, int len)
{
    const unsigned char *p_end;

    p_end = p + len;
    for(; p < p_end; p++) {
        h = ((h << 8) + p[0]) % PROPS_HASH_SIZE;
    }
    return h;
}

static unsigned int hash_props(CSSState *props)
{
    unsigned int h;

    /* since no holes are the the structure, we can compute the hash on
       the bytes */
    h = hash_bytes(1, (unsigned char *)props, PROPS_SIZE);
    return h;
}

static int is_equal_props(CSSState *p1, CSSState *p2)
{
    if (memcmp(p1, p2, PROPS_SIZE) != 0)
        return 0;
    return 1;
}

/* allocate a memory slot for 'props' which is shared with others */
static CSSState *allocate_props(CSSContext *s, CSSState *props)
{
    CSSState **pp, *p;
    pp = &s->hash_props[hash_props(props)];
    for(;;) {
        p = *pp;
        if (!p)
            break;
        /* if properties are already there, then no need to allocate */
        if (is_equal_props(p, props))
            return p;
        pp = &p->hash_next;
    }
    /* add new props */
    p = malloc(sizeof(CSSState));
    if (!p)
        return NULL;
    s->nb_props++;
    memcpy(p, props, sizeof(CSSState));
    *pp = p;
    p->hash_next = NULL;
    return p;
}

/* free one CSSState */
static void free_props(CSSState *props)
{
    free(props);
}

static int css_compute_block(CSSContext *s, CSSBox *box, 
                             CSSState *parent_props);

static CSSBox *add_before_after_box(CSSContext *s, 
                                    CSSBox *box, int pelement)
{
    CSSState pelement_props1, *pelement_props = &pelement_props1;
    CSSBox *box1;
    char *content;

    css_eval(s, pelement_props, box, pelement, box->props);
    if (!pelement_props->content) 
        return NULL;
    content = eval_content(s, pelement_props->content, box);
    if (!content)
        return NULL;
    box1 = css_new_box(CSS_ID_NIL, NULL);
    if (!box1) 
        return NULL;
    css_compute_block(s, box1, pelement_props);
    
    css_set_text_string(box1, content);
    free(content);
    /* XXX: make child box */
    return box1;
}

/* NOTE: 'buf' must be large enough */
/* XXX: find a shorter code :-) */
static void css_to_roman(char *buf, int n)
{
    int n1, n10;
    static const char roman_digits[] = "IVXLCDM";
    const char *p;
    char buf1[17], *q;

    if (n <= 0 || n >= 4000) {
        sprintf(buf, "%d", n);
        return;
    }

    p = roman_digits;
    q = buf1;
    while (n != 0) {
        n10 = n % 10;
        n1 = n10 % 5;
        if (n1 == 4) {
            *q++ = p[1 + (n10 == 9)];
            *q++ = p[0];
        } else {
            while (n1--)
                *q++ = p[0];
            if (n10 >= 5)
                *q++ = p[1];
        }
        n = n / 10;
        p += 2;
    }
    while (--q >= buf1)
        *buf++ = *q;
    *buf = '\0';
}

/* if adjust is true, then always use zero base and increment if
   necessary */
static void css_counter_str(char *text, int text_size, 
                            int index, int list_style_type, int adjust)
{
    /* insert marker text */
    switch(list_style_type) {
    case CSS_LIST_STYLE_TYPE_DISC:
        strcpy(text, "o");
        break;
    case CSS_LIST_STYLE_TYPE_CIRCLE:
        strcpy(text, "o");
        break;
    case CSS_LIST_STYLE_TYPE_SQUARE:
        strcpy(text, ".");
        break;
    case CSS_LIST_STYLE_TYPE_DECIMAL:
        snprintf(text, text_size, "%d", index + adjust);
        goto add_dot;
    case CSS_LIST_STYLE_TYPE_LOWER_ALPHA:
    case CSS_LIST_STYLE_TYPE_UPPER_ALPHA:
        if (index > 25)
            index = 25;
        text[0] = 'A' + index;
        text[1] = '\0';
        goto add_dot;
    case CSS_LIST_STYLE_TYPE_LOWER_ROMAN:
    case CSS_LIST_STYLE_TYPE_UPPER_ROMAN:
        css_to_roman(text, index + adjust);
    add_dot:
        if (adjust)
            pstrcat(text, text_size, ".");
        break;
    default:
        text[0] = '\0';
        break;
    }
    if (list_style_type == CSS_LIST_STYLE_TYPE_LOWER_ALPHA ||
        list_style_type == CSS_LIST_STYLE_TYPE_LOWER_ROMAN)
        css_strtolower(text, text_size);
}

/* add a marker or an inline box to a list item box */
static CSSBox *add_marker_box(CSSContext *s, CSSBox *box)
{
    CSSState marker_props1, *marker_props = &marker_props1, *aprops;
    CSSBox *box1;
    char text[256];
    int index;
    int position;

    box1 = css_new_box(CSS_ID_NIL, NULL);
    if (!box1)
        return NULL;
    
    position = box->props->list_style_position;

    /* create marker properties */
    *marker_props = *box->props;
    if (position == CSS_LIST_STYLE_POSITION_OUTSIDE) {
        marker_props->display = CSS_DISPLAY_MARKER;
        marker_props->bgcolor = COLOR_TRANSPARENT;
    } else {
        /* if inside, then create it as an inline box */
        marker_props->display = CSS_DISPLAY_INLINE;
    }
    aprops = allocate_props(s, marker_props);
    if (!aprops) {
        css_delete_box(box1);
        return NULL;
    }
    box1->props = aprops;

    index = get_counter(s, CSS_ID_list_item);
    incr_counter(s, CSS_ID_list_item, 1);

    css_counter_str(text, sizeof(text), 
                    index, marker_props->list_style_type, 1);

    if (position == CSS_LIST_STYLE_POSITION_INSIDE) {
        /* add an extra space if inside */
        pstrcat(text, sizeof(text), " ");
    }
    css_set_text_string(box1, text);
    if (marker_props->display == CSS_DISPLAY_MARKER) {
        CSSBox *box2;
        css_make_child_box(box1); /* add the inline text inside */
        /* XXX: alloc error testing ? */
        box2 = box1->u.child.first;
        marker_props->display = CSS_DISPLAY_INLINE;
        aprops = allocate_props(s, marker_props);
        box2->props = aprops;
    }
    return box1;
}

/* compute the CSS properties of a box */
static int css_compute_block(CSSContext *s, CSSBox *box, 
                             CSSState *parent_props)
{
    CSSState *aprops;
    CSSState props1, *props = &props1;
    CSSBox *box1, *box_next, **pbox;
    int pelement_found;
    CSSCounterValue *counter_stack;

    pelement_found = css_eval(s, props, box, 0, parent_props);

    /* allocate the properties for this box */
    aprops = allocate_props(s, props);
    if (!aprops)
        return -1;
    box->props = aprops;

    /* if the box is of type block, then it must contains childs,
       so we add a child as a dummy box */
    if (props->display != CSS_DISPLAY_INLINE &&
        box->content_type != CSS_CONTENT_TYPE_CHILDS &&
        box->content_type != CSS_CONTENT_TYPE_IMAGE) {
        css_make_child_box(box);
    }

    /* if boxes are inside, then evaluate their properties too */
    if (box->content_type == CSS_CONTENT_TYPE_CHILDS) {
        /* other boxes are inside: handle them */

        counter_stack = push_counters(s);

        for(box1 = box->u.child.first; box1 != NULL; box1 = box_next) {
            /* need to take next here because of :after inserted boxes */
            box_next = box1->next;
            if (css_compute_block(s, box1, props) < 0)
                return -1;
        }
        
        pop_counters(s, counter_stack);
        
        /* for :after and :before we create new boxes at the start or
           the end of the child list */
        /* XXX: mark them as temporary */
        box1 = NULL;
        if (pelement_found & CSS_PCLASS_BEFORE) {
            box1 = add_before_after_box(s, box, CSS_PCLASS_BEFORE);
            if (box1) {
                box1->next = box->u.child.first;
                box->u.child.first = box1;
                box1->parent = box;
            }
        }
        /* for list items, we must generate a marker/inline box, unless it
           was already generated by ':before' */
        if (props->display == CSS_DISPLAY_LIST_ITEM && 
            (!box1 || box1->props->display != CSS_DISPLAY_MARKER)) {
            box1 = add_marker_box(s, box);
            /* add it as first box */
            box1->next = box->u.child.first;
            box->u.child.first = box1;
            box1->parent = box;
        }

        if (pelement_found & CSS_PCLASS_AFTER) {
            box1 = add_before_after_box(s, box, CSS_PCLASS_AFTER);
            if (box1) {
                pbox = &box->u.child.first;
                while (*pbox != NULL)
                    pbox = &(*pbox)->next;
                *pbox = box1;
                box1->next = NULL;
            }
        }
    } else {
        /* inline case for before & after : insert directly in list */
        if (pelement_found & CSS_PCLASS_BEFORE) {
            box1 = add_before_after_box(s, box, CSS_PCLASS_BEFORE);
            /* add before box */
            if (box1) {
                pbox = &box->parent->u.child.first;
                while (*pbox != box)
                    pbox = &(*pbox)->next;
                box1->next = *pbox;
                *pbox = box1;
                box1->parent = box->parent;
            }
        }
        if (pelement_found & CSS_PCLASS_AFTER) {
            box1 = add_before_after_box(s, box, CSS_PCLASS_AFTER);
            /* add after box */
            if (box1) {
                box1->next = box->next;
                box->next = box1;
                box1->parent = box->parent;
            }
        }
    }
    return 0;
}

/* compute the CSS properties of a complete document */
int css_compute(CSSContext *s, CSSBox *box)
{
    CSSState props1, *default_props = &props1;
    int ret;

    //    css_dump(box);
    set_default_props(s, default_props);
    s->counter_stack_base = NULL;
    s->counter_stack_ptr = NULL;
    ret = css_compute_block(s, box, default_props);
    pop_counters(s, NULL);

    //    printf("nb_props=%d\n", s->nb_props);
    return ret;
}

/* split a css inline box at text offset 'offset' */
/* XXX: handle last_space */
static void css_box_split(CSSBox *box1, int offset)
{
    CSSBox *box2;

#if 0
    {
        dprintf("css_box_split: box=%p %d\n", box1, offset);
        if (!(offset >= box1->u.buffer.start &&
              offset < box1->u.buffer.end)) {
            dprintf("css_box_split: error offset=%d start=%lu end=%lu\n",
                    offset, box1->u.buffer.start, box1->u.buffer.end);
        }
    }
#endif    

    box2 = malloc(sizeof(CSSBox));
    if (!box2) 
        return;
    memset(box2, 0, sizeof(CSSBox));
    box2->split = 1;
    box2->props = box1->props; /* same properties */
    box2->content_type = box1->content_type;
    box2->content_eol = box1->content_eol;
    box1->content_eol = 0;
    box2->u.buffer.start = offset;
    box2->u.buffer.end = box1->u.buffer.end;
    box1->u.buffer.end = offset;
    box2->next = box1->next;
    box1->next = box2;
    box2->embedding_level = box1->embedding_level;
    box2->parent = box1->parent;
}

/******************************************************************/
/* text layout */

typedef struct LayoutOutput {
    int margin_top, margin_bottom;
    int baseline;
    int min_width, max_width;
} LayoutOutput;

typedef struct FloatBlock {
    /* absolute position of the float, including h margins */
    int x, y, width, height;
    int float_type; /* -1 indicate that the float is not layouted yet */
    CSSBox *box;
    struct FloatBlock *next;
} FloatBlock;

typedef struct LayoutState {
    CSSContext *ctx;
    FloatBlock *first_float;
} LayoutState;

static int css_layout_block(CSSContext *s, LayoutOutput *block_layout,
                            CSSBox *block_box);
static int css_layout_block_min_max(CSSContext *s, 
                                    int *min_width_ptr, int *max_width_ptr,
                                    CSSBox *block_box);

typedef struct BidirAttrState {
    CSSContext *ctx;
    TypeLink *list_end;
    TypeLink *list_ptr; /* current type pointer */
    FriBidiCharType ltype; /* last type */
    int pos; /* current char position */
} BidirAttrState;

static void bidir_compute_attributes_box(BidirAttrState *s, CSSBox *box)
{
    CSSState *props = box->props;
    int c, pos;
    unsigned long offset;
    TypeLink *p;
    FriBidiCharType type, ltype;
    NextCharFunc nextc = get_nextc(box);
    int bidi_mode;

    /* init the embedding_level to a known value to permit
       multiple layouts */
    box->embedding_level = 0;

    pos = s->pos;
    p = s->list_ptr;
    ltype = s->ltype;

    /* add bidi embed/override if needed */
    if (props->unicode_bidi != CSS_UNICODE_BIDI_NORMAL) {
        if (props->unicode_bidi == CSS_UNICODE_BIDI_EMBED) {
            if (props->direction == CSS_DIRECTION_LTR)
                type = FRIBIDI_TYPE_LRE;
            else
                type = FRIBIDI_TYPE_RLE;
        } else {
            if (props->direction == CSS_DIRECTION_LTR)
                type = FRIBIDI_TYPE_LRO;
            else
                type = FRIBIDI_TYPE_RLO;
        }
        if (type != ltype && p < s->list_end) {
            p->type = type;
            p->pos = pos;
            p->len = 1;
            p++;
            ltype = type;
        } else {
            p[-1].len++;
        }
    }

    if (props->display == CSS_DISPLAY_INLINE_TABLE ||
        props->display == CSS_DISPLAY_INLINE_BLOCK) {
        /* add a neutral type for images or inline tables */
        type = FRIBIDI_TYPE_ON;
        if (type != ltype && p < s->list_end) {
            p->type = type;
            p->pos = pos;
            p->len = 1;
            p++;
            ltype = type;
        } else {
            p[-1].len++;
        }
        pos++;
    } else {
        offset = box->u.buffer.start;
        bidi_mode = props->bidi_mode;
        while (offset < box->u.buffer.end) {
            c = nextc(s->ctx->b, &offset);
            pos++;
            if (bidi_mode == CSS_BIDI_MODE_TEST) 
                type = fribidi_get_type_test(c);
            else
                type = fribidi_get_type(c);
            /* if not enough room, increment last link */
            if (type != ltype && p < s->list_end) {
                p->type = type;
                p->pos = pos - 1;
                p->len = 1;
                p++;
                ltype = type;
            } else {
                p[-1].len++;
            }
        }
    }

    /* add end of bidi embed/override if needed */
    if (props->unicode_bidi != CSS_UNICODE_BIDI_NORMAL) {
        type = FRIBIDI_TYPE_PDF;
        if (type != ltype && p < s->list_end) {
            p->type = type;
            p->pos = pos;
            p->len = 1;
            p++;
            ltype = type;
        } else {
            p[-1].len++;
        }
    }

    s->pos = pos;
    s->list_ptr = p;
    s->ltype = ltype;
}

/* max_size should be >= 2 */
/* XXX: add CSS unicode-bidi property handling */
static int bidir_compute_attributes(CSSContext *ctx, TypeLink *list_tab, int max_size, 
                                    CSSBox *first_box)
{
    CSSBox *box;
    TypeLink *p;
    BidirAttrState bidir_state, *s = &bidir_state;

    p = list_tab;
    /* Add the starting link */
    p->type = FRIBIDI_TYPE_SOT;
    p->len = 0;
    p->pos = 0;
    p++;
    
    s->ctx = ctx;
    s->list_end = list_tab + max_size - 1;
    s->list_ptr = p;
    s->ltype = FRIBIDI_TYPE_SOT;
    s->pos = 0;

    for(box = first_box; box != NULL; box = box->next_inline) {
        bidir_compute_attributes_box(s, box);
    }

    /* Add the ending link */
    p = s->list_ptr;
    p->type = FRIBIDI_TYPE_EOT;
    p->len = 0;
    p->pos = s->pos;
    p++;

    return p - list_tab;
}

/* split the boxes so that the embedding is constant inside a
   box. */

typedef struct BidirSplitState {
    CSSContext *ctx;
    TypeLink *l;
    int pos;
} BidirSplitState;

static void css_bidir_split_box(BidirSplitState *s,  CSSBox *box)
{
    CSSState *props = box->props;
    TypeLink *l;
    int c, pos;
    unsigned long offset;
    NextCharFunc nextc;

    l = s->l;
    pos = s->pos;
    if (props->display == CSS_DISPLAY_INLINE_TABLE ||
        props->display == CSS_DISPLAY_INLINE_BLOCK) {
        /* single position increment, as in a attribute compute */
        if (pos >= l[1].pos) 
            l++;
        pos++;
    } else {
        offset = box->u.buffer.start;
        nextc = get_nextc(box);
        while (offset < box->u.buffer.end) {
            if (pos >= l[1].pos) {
                l++;
                if (offset > box->u.buffer.start &&
                    l[0].level != l[-1].level) {
                    /* different level : split box */
                    box->embedding_level = l[-1].level;
                    css_box_split(box, offset);
                    /* update next_inline field */
                    box->next->next_inline = box->next_inline;
                    box->next_inline = box->next;
                    goto the_end;
                }
            }
            c = nextc(s->ctx->b, &offset);
            pos++;
        }
    }
    box->embedding_level = l[0].level;
 the_end:
    s->l = l;
    s->pos = pos;
}


static void css_bidir_split(CSSContext *s, CSSBox *first_box, TypeLink *l)
{
    CSSBox *box;
    BidirSplitState bidir_split;

    bidir_split.ctx = s;
    bidir_split.l = l + 1;
    bidir_split.pos = 0;
    for(box = first_box; box != NULL; box = box->next_inline) {
        css_bidir_split_box(&bidir_split, box);
    }
}

#define RLE_EMBEDDINGS_SIZE 128

typedef struct BidirComputeState {
    CSSContext *ctx;
    int inline_layout;
    CSSBox *first_inline; /* first inline box of current inline
                             formatting context */
    CSSBox **pbox; /* pointer to last next_inline field */
} BidirComputeState;

static void bidir_start_inline(BidirComputeState *s)
{
    s->pbox = &s->first_inline;
    s->inline_layout = 1;
}

static void bidir_end_inline(BidirComputeState *s)
{
    TypeLink embeds[RLE_EMBEDDINGS_SIZE];
    int embedding_max_level;
    FriBidiCharType base;

    /* mark last box */
    *s->pbox = NULL;
    
    /* now we can do the bidir compute easily */
    if (bidir_compute_attributes(s->ctx, embeds, RLE_EMBEDDINGS_SIZE,
                                 s->first_inline) > 2) {
        base = FRIBIDI_TYPE_WL;
        fribidi_analyse_string(embeds, &base, &embedding_max_level);
#if 0
        {
            TypeLink *p;
            p = embeds;
            printf("bidi_start:\n");
            for(;;) {
                printf("type=%d pos=%d len=%d level=%d\n", p->type, p->pos, p->len, p->level);
                if (p->type == FRIBIDI_TYPE_EOT)
                    break;
                p++;
            }
        }
#endif
        /* optimization: no need to split if no embedding */
        if (embedding_max_level > 0)
            css_bidir_split(s->ctx, s->first_inline, embeds);
    }
    
    /* terminate inline layout */
    s->inline_layout = 0;
}

static int css_layout_bidir_block(CSSContext *ctx, CSSBox *box);

/* layout a box inside a block or inline formatting context */
static int css_layout_bidir_box(BidirComputeState *s, CSSBox *box)
{
    CSSState *props = box->props;
    CSSBox *box1;
    int ret;

    if (props->block_float != CSS_FLOAT_NONE) {
        if (props->display != CSS_DISPLAY_NONE)
            css_layout_bidir_block(s->ctx, box);
    } else {
        /* now do the layout, according to the display type */
        switch(props->display) {
        case CSS_DISPLAY_INLINE:
        case CSS_DISPLAY_INLINE_TABLE:
        case CSS_DISPLAY_INLINE_BLOCK:
            if (!s->inline_layout) 
                bidir_start_inline(s);
            if (props->display == CSS_DISPLAY_INLINE_TABLE ||
                props->display == CSS_DISPLAY_INLINE_BLOCK) {
                ret = css_layout_bidir_block(s->ctx, box);
                if (ret)
                    return ret;
            }
            if (props->display != CSS_DISPLAY_INLINE_TABLE &&
                props->display != CSS_DISPLAY_INLINE_BLOCK &&
                box->content_type == CSS_CONTENT_TYPE_CHILDS) {
                /* recurse inside in same formatting context */
                for(box1 = box->u.child.first; box1 != NULL; box1 = box1->next) {
                    ret = css_layout_bidir_box(s, box1);
                    if (ret)
                        return ret;
                }
            } else {
                /* add the box in the inline box list */
                *s->pbox = box;
                s->pbox = &box->next_inline;
            }
            break;
        case CSS_DISPLAY_NONE:
            break;
        default:
            /* all other boxes are considered as block boxes */
            if (s->inline_layout) 
                bidir_end_inline(s);
            ret = css_layout_bidir_block(s->ctx, box);
            if (ret)
                return ret;
            break;
        }
    }
    return 0;
}

/* bidir compute of the inside of a block */
static int css_layout_bidir_block(CSSContext *ctx, CSSBox *box)
{
    BidirComputeState bidi_state, *s = &bidi_state; 
    CSSBox *box1;
    int ret;

    if (box->content_type != CSS_CONTENT_TYPE_CHILDS)
        return 0;
    
    s->inline_layout = 0;
    s->ctx = ctx;
    for(box1 = box->u.child.first; box1 != NULL; box1 = box1->next) {
        ret = css_layout_bidir_box(s, box1);
        if (ret)
            return ret;
    }
    if (s->inline_layout) 
        bidir_end_inline(s);
    return 0;
}


typedef struct {
    CSSBox *box;
    short baseline_delta; /* delta to the line baseline */
    short ascent;         /* font ascent */
} InlineBox;

static void reverse_boxes(InlineBox *str, int len)
{
    int i, len2 = len / 2;
    
    for (i = 0; i < len2; i++) {
	InlineBox tmp = str[i];
	str[i] = str[len - 1 - i];
	str[len - 1 - i] = tmp;
    }
}

static void embed_boxes(InlineBox *line, int len, int level_max)
{
    int level, pos, p;

    for (level = level_max; level > 0; level--) {
        pos = 0;
        while (pos < len) {
            if (line[pos].box->embedding_level >= level) {
                /* find all chars >= level */
                for(p = pos + 1; p < len && line[p].box->embedding_level >= level; p++);
                reverse_boxes(line + pos, p - pos);
                pos = p + 1;
            } else {
                pos++;
            }
        }
    }
}

#define NB_LINE_BOXES_MAX 100
#define BOX_STACK_SIZE    200

/* layout context type */
#define LAYOUT_TYPE_BLOCK  0
#define LAYOUT_TYPE_INLINE 1

typedef struct InlineLayout {
    /* info for both inline/block layout */
    LayoutState *layout_state;
    CSSContext *ctx;
    int x0, y0; /* origin of the current inline layout context, used
                   when floats are present */
    int total_width;  /* total width available (not counting floats) */
    int y;      /* current y (top of current line) */
    int layout_type;
    int is_first_box;
    int margin_top;
    int last_ymargin;
    int line_count;
    int first_line_baseline; /* baseline of the first line */
    CSSBox *marker_box;   /* pointer to the last marker box */
    int marker_baseline;  /* marker baseline position */
    
    /* inline layout context */
    int x; /* current x */
    int last_space; /* true if last char was a space */
    /* memorize the position of the beginning of the current word */
    int index_bow, offset_bow, width_bow;
    int lastwordspace;
    int line_pos; /* box number on the current line */
    int char_pos; /* character count on the current line */
    int xstart, avail_width; /* line position including floats */
    int word_index;
    int box_stack_index; /* current position in box stack */
    /* min/max width computation */
    int compute_min_max;
    int min_width, max_width;
    
    InlineBox line_boxes[NB_LINE_BOXES_MAX];
    /* fragment layout = part of line */
    unsigned int word_buf[MAX_WORD_SIZE];
    int word_offsets[MAX_WORD_SIZE];
    /* the box stack contains the boxes which where after the last
       word of the current line and which must be layouted on the next
       line */
    CSSBox *box_stack[BOX_STACK_SIZE];
} InlineLayout;


/* layout a float box and position it */
static int css_layout_float(InlineLayout *s, FloatBlock *b)
{
    CSSBox *box = b->box;
    CSSState *props = box->props;
    LayoutOutput layout;
    FloatBlock *b1;
    int lmargin, rmargin, tmargin, bmargin, y, y2, x1, x2, y_next;

    /* set the width */
    if (props->width == CSS_AUTO) {
        int min_w, max_w;
        /* layout to get the dimensions */
        css_layout_block_min_max(s->ctx, &min_w, &max_w,  box);
        box->width = max_w;
    } else {
        box->width = props->width;
    }
    if (props->height != CSS_AUTO)
        box->height = props->height;

#if 0
    printf("layout float: %s w=%d h=%d\n", 
           css_ident_str(box->tag), box->width, box->height);
#endif
    /* layout the interior */
    if (css_layout_block(s->ctx, &layout, box)) {
        free(b);
        return -1;
    }
    /* add the float in the float list */
    b->float_type = props->block_float;
    /* compute the position of the float */
    lmargin = props->margin.x1 + props->border.x1 + props->padding.x1;
    rmargin = props->margin.x2 + props->border.x2 + props->padding.x2;
    tmargin = props->border.y1 + props->padding.y1;
    bmargin = props->border.y2 + props->padding.y2;
    b->box = box;
    b->width = lmargin + box->width + rmargin;
    b->height = tmargin + box->height + bmargin;

    /* we go down until we find no floats or enough space to put the
       new float */
    y = s->y0 + s->y;
    for(;;) {
        x1 = s->x0;
        x2 = s->x0 + s->total_width;
        y_next = MAXINT;
        for(b1 = s->layout_state->first_float; b1 != NULL; b1 = b1->next) {
            if (b1->float_type != -1) {
                /* if intersection in y, then update x1 and x2 */
                y2 = b1->y + b1->height;
                if (!(y + b->height <= b1->y || y >= y2)) {
                    if (b1->float_type == CSS_FLOAT_LEFT)
                        x1 = max(x1, b1->x + b1->width);
                    else
                        x2 = min(x2, b1->x);
                    /* compute minimum next y */
                    if (y2 < y_next)
                        y_next = y2;
                }
            }
        }
        /* if enough space, then ok */
        if (b->width <= (x2 - x1))
            break;
        /* if no more boxes, we cannot do better by going down */
        if (x1 == s->x0 &&
            x2 == s->x0 + s->total_width)
            break;
        /* go down by the minimum amount of space to skip one box */
        y = y_next;
    }

    /* compute box position */
    if (b->float_type == CSS_FLOAT_LEFT)
        b->x = x1;
    else
        b->x = x2 - b->width;
    b->y = y;

    box->x = (b->x - s->x0) + lmargin;
    box->y = (b->y - s->y0) + tmargin;

    return 0;
}    

/* layout all non layouted floats. Need to work that way because
   floats cannot be positionned while composing the current line */
static int css_layout_floats(InlineLayout *s)
{
    FloatBlock *b;
    int ret;
    
    for(b = s->layout_state->first_float; b != NULL; b = b->next) {
        if (b->float_type == -1) {
            ret = css_layout_float(s, b);
            if (ret)
                return ret;
        }
    }
    return 0;
}

/* analyse the floating blocks and compute the available line size
   (fields y, xstart and avail_width are modified) */
static void css_prepare_line(InlineLayout *s, int clear_type)
{
    LayoutState *l;
    FloatBlock *b, **pb;
    int x1, x2, y;

    /* layout all pending floats */
    css_layout_floats(s);

    l = s->layout_state;
    /* special case handled separately for speed : no floats */
    if (!l->first_float) {
        s->xstart = 0;
        s->avail_width = s->total_width;
        return;
    }
    x1 = s->x0;
    x2 = s->x0 + s->total_width;
    y = s->y0 + s->y;
    if (clear_type != CSS_CLEAR_NONE) {
        pb = &l->first_float;
        /* first we handle clear and suppress the box from the list */
        while ((*pb) != NULL) {
            b = *pb;
            if (b->float_type != -1) {
                if (y >= b->y && y < b->y + b->height) {
                    if (((clear_type & CSS_CLEAR_LEFT) &&
                         b->float_type == CSS_FLOAT_LEFT) ||
                        ((clear_type & CSS_CLEAR_RIGHT) &&
                         b->float_type == CSS_FLOAT_RIGHT)) {
                        /* test if the box is in the rendering line */
                        if (!(b->x + b->height <= x1 ||
                              b->x >= x2)) {
                            /* update y to go just after the cleared box */
                            y = max(y, (*pb)->y + b->height);
                        }
                    }
                }
                /* suppress from the float list if no longer active */
                if (y >= b->y + b->height) {
                    *pb = (*pb)->next;
                    continue;
                }
            }
            pb = &(*pb)->next;
        }
    }

    /* then we handle the floats */
    pb = &l->first_float;
    /* first we handle clear and suppress the box from the list */
    while ((*pb) != NULL) {
        b = *pb;
        if (b->float_type != -1) {
            if (y >= b->y && y < b->y + b->height) {
                if (b->float_type == CSS_FLOAT_LEFT) {
                    x1 = max(x1, b->x + b->width);
                } else {
                    x2 = min(x2, b->x);
                }
            }
            /* suppress from the float list if no longer active */
            if (y >= b->y + b->height) {
                *pb = (*pb)->next;
                continue;
            }
        }
        pb = &(*pb)->next;
    }

    /* compute x and y in local coordinates */
    s->y = y - s->y0;
    s->xstart = x1 - s->x0;
    s->avail_width = x2 - x1;
}

/* do the layout of a line of boxes */
static void css_flush_line(InlineLayout *s,
                           InlineBox *line_boxes, int nb_boxes,
                           CSSState *line_props)
{
    CSSBox *box;
    CSSState *props;
    int available_width, line_width, baseline, line_height, descent;
    int level_max, i, x, y, b, v, left_pad, right_pad;
    InlineBox *box_table, *box_table1, *ib;

    if (s->compute_min_max) {
        s->max_width = max(s->max_width, s->x);
        goto the_end;
    }
    available_width = s->avail_width;
    
    /* compute global line parameters */
    line_width = 0;
    baseline = 0;
    descent = 0;
    level_max = 0;
    for(i=0;i<nb_boxes;i++) {
        box = line_boxes[i].box;
        props = box->props;
        line_width += box->width;
        if (props->display != CSS_DISPLAY_INLINE) {
            line_width += props->margin.x1 + props->border.x1 + props->padding.x1 +
                props->padding.x2 + props->margin.x2 + props->border.x2;
        }
        /* adjust baseline */
        b = line_boxes[i].ascent;
        v = line_boxes[i].baseline_delta;
        if (v < 0)
            b += v;
        if (b > baseline)
            baseline = b;
        /* adjust descent */
        b = box->height - line_boxes[i].ascent;
        if (v > 0)
            b += v;
        if (b > descent)
            descent = b;
        if (box->embedding_level > level_max)
            level_max = box->embedding_level;
    }
    /* XXX: correct line_height behaviour (value normal, etc...) */
    line_height = baseline + descent;
    if (line_props->line_height != CSS_AUTO)
        line_height = line_props->line_height;

    if (level_max > 0) {
        /* needed to do bidir reordering */
        box_table = malloc(sizeof(InlineBox) * nb_boxes);
        if (box_table) {
            /* record the logical order of the boxes */
            memcpy(box_table, line_boxes, nb_boxes * sizeof(InlineBox));
            /* rearrange them to match visual order */
            embed_boxes(box_table, nb_boxes, level_max);
        }
    } else {
        box_table = NULL;
    }

    if (box_table)
        box_table1 = box_table;
    else
        box_table1 = line_boxes;

    /* now do the real layout ! */
    switch(line_props->text_align) {
    case CSS_TEXT_ALIGN_LEFT:
    default:
        x = 0;
        break;
    case CSS_TEXT_ALIGN_RIGHT:
        x = available_width - line_width;
        break;
    case CSS_TEXT_ALIGN_CENTER:
        x = (available_width - line_width) / 2;
        break;
    }
    if (x < 0)
        x = 0;
    x += s->xstart;

    y = s->y;
    for(i=0;i<nb_boxes;i++) {
        ib = &box_table1[i];
        /* get next box in visual order */
        box = ib->box;
        props = box->props;
        if (props->display != CSS_DISPLAY_INLINE) {
            left_pad = props->padding.x1 + props->margin.x1 + props->border.x1;
            right_pad = props->margin.x2 + props->border.x2 + props->padding.x2;
        } else {
            left_pad = 0;
            right_pad = 0;
        }
        x += left_pad;
        box->x = x;
        switch(box->props->vertical_align) {
        case CSS_VERTICAL_ALIGN_TOP:
            box->y = y;
            break;
        case CSS_VERTICAL_ALIGN_BOTTOM:
            box->y = y + line_height - box->height;
            break;
        default:
            /* align to baseline */
            box->y = y + baseline + ib->baseline_delta - ib->ascent;
            break;
        }
        /* put real ascent in box */
        box->ascent = ib->ascent;
        /* correct borders */
        if (props->display != CSS_DISPLAY_INLINE) {
            box->y += props->margin.y1 + props->border.y1 + props->padding.y1;
        }
        x += box->width;
        x += right_pad;
    }
    /* also align marker box if present */
    box = s->marker_box;
    if (box) {
        box->y = y + baseline - s->marker_baseline;
        s->marker_box = NULL;
    }
    /* save baseline (for vertical align of table cells or markers) */
    if (s->line_count == 0)
        s->first_line_baseline = baseline;
    s->line_count++;

    y += line_height;
    s->y = y;

    if (box_table)
        free(box_table);
    
    /* prepare for next line */
 the_end:
    s->x = 0;
    s->line_pos = 0;
    s->char_pos = 0;
    s->index_bow = 0;
    s->offset_bow = 0;
    s->width_bow = 0;
}

static QEFont *css_select_font(QEditScreen *screen, CSSState *props)
{
    int style;

    /* select the correct font */
    style = 0;
    if (props->font_style == CSS_FONT_STYLE_ITALIC)
        style |= QE_STYLE_ITALIC;
    if (props->font_weight == CSS_FONT_WEIGHT_BOLD ||
        props->font_weight == CSS_FONT_WEIGHT_BOLDER)
        style |= QE_STYLE_BOLD;
    if (props->text_decoration == CSS_TEXT_DECORATION_UNDERLINE)
        style |= QE_STYLE_UNDERLINE;
    else if (props->text_decoration == CSS_TEXT_DECORATION_LINE_THROUGH)
        style |= QE_STYLE_LINE_THROUGH;

    style |= props->font_family;
    return select_font(screen, style, props->font_size);
}

/* flush a text fragment. return non zero if the layout of the current
   box must be interrupted because a new line has been emitted */
static int css_flush_fragment(InlineLayout *s, CSSBox *box, CSSState *props,
                              QEFont *font)
{
    int w, line_size, split, ret, i, h;
    CSSBox *box_bow;
    QECharMetrics metrics;
    
    if (s->word_index == 0)
        return 0;

    /* flush the current word */
    /* XXX: convert to glyphs */
    text_metrics(s->ctx->screen, font, &metrics, s->word_buf, s->word_index);
    w = metrics.width;
#if 0
    {
        int i;
        printf("flush_word: '");
        for(i=0;i<s->word_index;i++)
            printf("%c", s->word_buf[i]);
        printf("' x=%d w=%d avail=%d white_space=%d word_index=%d\n", 
               s->x, w, s->avail_width, props->white_space, s->word_index);
    }
#endif
    if (s->compute_min_max) {
        /* min/max computation */
        if (props->white_space == CSS_WHITE_SPACE_NORMAL) {
            /* XXX: not quite exact */
            s->min_width = max(s->min_width, w);
        }
        s->x += w;
        if (props->white_space != CSS_WHITE_SPACE_NORMAL)
            s->min_width = max(s->min_width, s->x);
        ret = 0;
    } else if (props->white_space == CSS_WHITE_SPACE_PRE ||
        props->white_space == CSS_WHITE_SPACE_NOWRAP ||
        s->x + w <= s->avail_width ||
        (props->white_space == CSS_WHITE_SPACE_NORMAL && 
         s->index_bow == 0 && s->width_bow == 0)) {
        /* simple case : increment position */
        s->x += w;
        box->width += w;
        /* update the box height info if specific word metrics are
           different from font metrics (can happen in case of font
           fallback). XXX: need further checks for ascent patch in
           line_boxes */
        h = metrics.font_ascent + metrics.font_descent;
        if (h > box->height)
            box->height = h;
        if (metrics.font_ascent > s->line_boxes[s->line_pos - 1].ascent)
            s->line_boxes[s->line_pos - 1].ascent = metrics.font_ascent;
        ret = 0;
    } else {
        /* end of line reached */
        if (props->white_space == CSS_WHITE_SPACE_PREWRAP) {
            /* try to put as much chars as possible on the
               line (maybe none) */
            /* XXX: use dichotomy */
            for(;;) {
                s->word_index--;
                text_metrics(s->ctx->screen, font, &metrics, 
                             s->word_buf, s->word_index);
                w = metrics.width;
                if (s->x + w <= s->avail_width)
                    break;
            }
            /* put the end of the word on the next line */
            box->width += w;
            h = metrics.font_ascent + metrics.font_descent;
            if (h > box->height)
                box->height = h;
            if (metrics.font_ascent > s->line_boxes[s->line_pos - 1].ascent)
                s->line_boxes[s->line_pos - 1].ascent = metrics.font_ascent;
            s->index_bow = s->line_pos - 1;
            s->offset_bow = s->word_offsets[s->word_index];
        } else {
            /* correct the width of the box where the word starts
               (width_bow was stored at the beginning of the word) */
            s->line_boxes[s->index_bow].box->width = s->width_bow;
        }
        
        /* split the box containing the start of the word,
           if needed */
        box_bow = s->line_boxes[s->index_bow].box;
        if (s->offset_bow > box_bow->u.buffer.start) {
            /* split the box containing the start of the word */
            css_box_split(box_bow, s->offset_bow);
            /* include the start of the splitted box */
            split = 1;
            box_bow = box_bow->next;
        } else {
            split = 0;
        }

        /* push in the stack all remaining boxes for the next line */
        if (s->box_stack_index < BOX_STACK_SIZE)
            s->box_stack[s->box_stack_index++] = box_bow;
        for(i=s->index_bow + 1;i<s->line_pos;i++) {
            if (s->box_stack_index < BOX_STACK_SIZE)
                s->box_stack[s->box_stack_index++] = s->line_boxes[i].box;
        }

        /* do the line layout */
        /* XXX: we do not use exactly the right
           properties. The CSS2 spec is not precise enough */
        line_size = s->index_bow + split;
        css_flush_line(s, s->line_boxes, line_size, props);
        
        ret = 1;
    }
    s->word_index = 0;
    return ret;
}


/* XXX: find better values ? */
#define SUPER_PERCENT 80
#define SUB_PERCENT   40

/* Layout one inline box. Return the next box to layout (may be before
   'box' in case of line cut) */
static int css_layout_inline_box(InlineLayout *s, 
                                 CSSBox *box,
                                 int baseline)
{
    CSSState *props = box->props;
    unsigned long offset, offset0;
    int ch, space, eob, ret, box_stack_base, i;
    QEFont *font;
    NextCharFunc nextc;

    if (s->ctx->abort_func(s->ctx->abort_opaque))
        return -1;

    font = css_select_font(s->ctx->screen, props);

    if (!s->compute_min_max && box->parent &&
        props->vertical_align != CSS_VERTICAL_ALIGN_BASELINE &&
        props->vertical_align != CSS_VERTICAL_ALIGN_TOP &&
        props->vertical_align != CSS_VERTICAL_ALIGN_BOTTOM) {
        int ascent, descent;
        QEFont *parent_font;
        
        parent_font = css_select_font(s->ctx->screen, box->parent->props);
        ascent = parent_font->ascent;
        descent = parent_font->descent;

        /* align vertically with respect to the parent */
        switch(props->vertical_align) {
        case CSS_VERTICAL_ALIGN_SUPER:
            baseline -= (ascent * SUPER_PERCENT) / 100;
            break;
        case CSS_VERTICAL_ALIGN_SUB:
            baseline += (ascent * SUB_PERCENT) / 100;
            break;
        case CSS_VERTICAL_ALIGN_TEXT_TOP:
            baseline += font->ascent - ascent;
            break;
        case CSS_VERTICAL_ALIGN_TEXT_BOTTOM:
            baseline += descent - font->descent;
            break;
        case CSS_VERTICAL_ALIGN_MIDDLE:
            /* XXX: correct me */
            baseline += (font->ascent + font->descent - ascent - descent) / 2;
        break;
        default:
            /* top and bottom are handled in the line layout */
            break;
        }
    }

    if (props->display == CSS_DISPLAY_INLINE_TABLE ||
        props->display == CSS_DISPLAY_INLINE_BLOCK) {
        int w, w1;
        LayoutOutput layout;

        /* if first box on the line, then compute available size by
           taking floats into account */
        if (s->line_pos == 0 && !s->compute_min_max) {
            css_prepare_line(s, props->clear);
        }

        /* XXX: no pre like formatting handled here */
        if (props->width == CSS_AUTO) {
            int min_w, max_w;
            /* layout to get the dimensions */
            css_layout_block_min_max(s->ctx, &min_w, &max_w,  box);
            w1 = max_w;
        } else {
            w1 = props->width;
        }

        w = props->margin.x1 + props->border.x1 + props->padding.x1 + 
            w1 +
            props->padding.x2 + props->border.x2 + props->margin.x2;
        if (s->compute_min_max) {
            s->min_width = max(s->min_width, w);
            s->x += w;
        } else {
            box->width = w1;
            if (props->height != CSS_AUTO) {
                box->height = props->height;
            } else {
                /* we use the font width as default */
                box->height = font->ascent + font->descent;
            }

            if (css_layout_block(s->ctx, &layout, box))
                return -1;
            
            if (s->x + w <= s->avail_width ||
                s->x == 0) {
                s->x += w;
            } else {
                /* flush line if we cannot put the image */
                css_flush_line(s, s->line_boxes, s->line_pos, props);
                s->x += w;
            }
            /* add the box on the current line */
            {
                InlineBox *b = &s->line_boxes[s->line_pos];
                    
                b->box = box;
                b->baseline_delta = 0;
                b->ascent = box->height + 
                    props->margin.y1 + props->border.y1 + props->padding.y1 + 
                    props->padding.y2 + props->border.y2 + props->margin.y2;
            }
            s->line_pos++;
        }
        s->char_pos++;
        s->last_space = 0;
        s->lastwordspace = -1;
    } else if (box->content_type == CSS_CONTENT_TYPE_CHILDS) {
        CSSBox *box1;

        box1 = box->u.child.first;
        while (box1 != NULL) {
            ret = css_layout_inline_box(s, box1, baseline);
            if (ret)
                return ret;
            box1 = box1->next;
        }
    } else {

        box_stack_base = s->box_stack_index;

        /* if first box on the line, then compute available size by
           taking floats into account */
        if (s->line_pos == 0 && !s->compute_min_max) {
            css_prepare_line(s, props->clear);
        }

        nextc = get_nextc(box);

        offset = box->u.buffer.start;

        /* loop over each char of the box and separate into words */
        eob = 0;
        ch = 0; /* not used */
        space = 0; /* not used */
        offset0 = 0; /* not used */
        s->word_index = 0;
        
        if (!s->compute_min_max) {
            box->width = 0;
            box->last_space = s->last_space;
            /* compute vertical dimensions from the font parameters */
            box->height = font->ascent + font->descent;
                
            /* add the box on the current line */
            {
                InlineBox *b = &s->line_boxes[s->line_pos];
                    
                b->box = box;
                b->baseline_delta = baseline;
                b->ascent = font->ascent;
            }
            s->line_pos++;
        }

        for(;;) {
            /* end of block reached ? */
            if (offset >= box->u.buffer.end) {
                css_flush_fragment(s, box, props, font);
                break;
            } else {
                /* get next char */
                offset0 = offset;
                ch = nextc(s->ctx->b, &offset);

                /* special case: '\n' is handled in pre mode, or
                   special '\A' character in css content
                   property. */
                if ((ch == '\n' && 
                     props->white_space == CSS_WHITE_SPACE_PRE) ||
                    ch == CSS_CONTENT_EOL) {
                    s->last_space = 1;
                    /* cannot split in PRE mode, so no handling
                       necessary */
                    css_flush_fragment(s, box, props, font);
                    /* split box if '\n' is not at the end */
                    if (offset < box->u.buffer.end) {
                        css_box_split(box, offset);
                        /* add the splitted box in the stack for next line */
                        if (s->box_stack_index < BOX_STACK_SIZE)
                            s->box_stack[s->box_stack_index++] = box->next;
                    }
                    css_flush_line(s, s->line_boxes, s->line_pos, props);
                    break;
                }
                /* special case for tabulation in pre mode */
                if (ch == '\t' && 
                    (props->white_space == CSS_WHITE_SPACE_PRE || 
                     props->white_space == CSS_WHITE_SPACE_PREWRAP)) {
                    int tab_width, split, w;
                    /* XXX: should not wrap there */
                    css_flush_fragment(s, box, props, font);
                    /* split box after tab */
                    split = 0;
                    if (offset < box->u.buffer.end) {
                        css_box_split(box, offset);
                        /* add the splitted box in the stack for next line */
                        if (s->box_stack_index < BOX_STACK_SIZE)
                            s->box_stack[s->box_stack_index++] = box->next;
                        split = 1;
                    }
                    /* modify current position to take tab into account */
                    /* XXX: should compute that metrics before */
                    /* XXX: min/max compute ? */
                    tab_width = glyph_width(s->ctx->screen, font, ' ') * 8;
                    w = (tab_width - (s->x % tab_width));
                    box->width += w;
                    if (split)
                        break;
                }

                space = css_is_space(ch);
                /* collapse spaces if needed */
                if (space && s->last_space &&
                    (props->white_space == CSS_WHITE_SPACE_NORMAL ||
                     props->white_space == CSS_WHITE_SPACE_NOWRAP)) {
                    continue;
                }
                
                if ((s->word_index >= MAX_WORD_SIZE) ||
                    (s->word_index >= 1 && space != s->last_space)) { 
                    if (css_flush_fragment(s, box, props, font))
                        break;
                }
            }
            /* memorize the beginning of the word (needed because the
                   word may span several boxes) */
            if (s->word_index == 0 && space != s->lastwordspace) {
                s->index_bow = s->line_pos - 1;
                s->offset_bow = offset0;
                s->width_bow = box->width;
                s->lastwordspace = space;
            }
            if (space)
                ch = ' ';
            s->word_buf[s->word_index] = ch;
            s->word_offsets[s->word_index] = offset0;
            s->word_index++;
            s->char_pos++;
            s->last_space = space;
        }
        /* end of the current box */
        
        /* layout boxes at the end of the line if needed */
        /* XXX: use another function */
        for(i=box_stack_base; i < s->box_stack_index; i++) {
            css_layout_inline_box(s, s->box_stack[i], baseline);
        }
        /* remove all boxes from the stack */
        s->box_stack_index = box_stack_base;
    }
    return 0;
}

static void css_start_inline_layout(InlineLayout *s)
{
    s->x = 0;
    s->line_pos = 0;
    s->char_pos = 0;
    s->last_space = 1;
    s->lastwordspace = -1;
    s->index_bow = 0;
    s->offset_bow = 0;
    s->width_bow = 0;
    s->box_stack_index = 0;
    s->layout_type = LAYOUT_TYPE_INLINE;
}

static void css_end_inline_layout(InlineLayout *s)
{
    /* do the last line layout if any boxes remain */
    if (s->char_pos > 0) {
        /* XXX: we do not use exactly the right
           properties. The CSS2 spec is not precise enough */
        css_flush_line(s, s->line_boxes, s->line_pos, 
                       s->line_pos > 0 ? s->line_boxes[0].box->props : NULL);
    }
    /* layout all remaining floats */
    if (!s->compute_min_max) {
        css_layout_floats(s);
        s->marker_box = NULL;
    }
    s->layout_type = LAYOUT_TYPE_BLOCK;
}

/******************************************************************/
/* table layout */

typedef struct ColStruct {
    int width;   /* column width, result from layout fixed or auto */
    int width_fixed;
    /* auto layout only */
    int min_width;
    int max_width;
    /* the following used only during render_table_row() */
    CSSBox *cell;
    int baseline; 
    int height;
    int vertical_align;
    int row_span_left; /* used for row span handling. If non zero, then a
                          cell from a preceeding row is extending
                          downwards. also used in auto layout */
    int prev_row_height; /* used if row span to compute cell height */
} ColStruct;

typedef struct TableLayout {
    CSSContext *ctx;
    /* min/max compute */
    int compute_min_max;
    int min_width;
    int max_width;
    /* table border information */
    int border_collapse, border_h, border_v;
    ColStruct *cols;
    int nb_cols;             /* number of columns */
    CSSBox *caption_box;
    /* TABLE_LAYOUT_COMPUTE_COL_FIXED state */
    int nb_cols_allocated;   /* currently allocate cols array size */
    int column_index;        /* index for use with table-column boxes */
    /* TABLE_LAYOUT_RENDER state */
    int y; /* when rendering, current y value */
    int table_width;
    int row;
} TableLayout;

#define COL_INCR 10

/* allocate a new column */
static void allocate_column(TableLayout *s)
{
    s->nb_cols++;
    if (s->nb_cols > s->nb_cols_allocated) {
        s->nb_cols_allocated = s->nb_cols_allocated + COL_INCR;
        s->cols = realloc(s->cols, s->nb_cols_allocated * sizeof(ColStruct));
        memset(s->cols + s->nb_cols_allocated - COL_INCR, 0, 
               COL_INCR * sizeof(ColStruct));
    }
}

/* rounding rule for border which always gives correct sum :
   div2rnd(x, i) + div2rnd(x, i + 1) = x
*/
static inline int div2rnd(int x, int i)
{
    return (x + (i & 1)) / 2;
}

/**************************************************************/
/* 'fixed' layout algorithm */

static void layout_table_row_fixed(TableLayout *s, CSSBox *row)
{
    CSSBox *cell;
    CSSState *cell_props;
    int i, colspan, w;

    /* we only process the first row */
    if (s->row != 0)
        return;

    for(cell = row->u.child.first; cell != NULL; 
        cell = cell->next) {
        cell_props = cell->props;
        if (cell_props->display == CSS_DISPLAY_TABLE_CELL) {
            colspan = cell_props->column_span;
            if (colspan < 1)
                colspan = 1;
            for(i=0;i<colspan;i++) 
                allocate_column(s);
            
            /* if a width is specified, we use it */
            if (cell_props->width != CSS_AUTO) {
                /* XXX: margins ? */
                /* XXX: dispatch width */
                w = cell_props->width / colspan;
                if (w < 1)
                    w = 1;
                for(i=0;i<colspan;i++) {
                    s->cols[s->nb_cols - colspan + i].width_fixed = 1;
                    s->cols[s->nb_cols - colspan + i].width = 
                        max(w, s->cols[s->nb_cols - colspan + i].width);
                }
            }
        }
    }
}

static int layout_table_fixed(TableLayout *s, CSSBox *parent_box)
{
    CSSState *props;
    CSSBox *box;

    /* get the first row and examine each column element */
    box = parent_box->u.child.first;
    while (box != NULL) {
        props = box->props;
        switch(props->display) {
        case CSS_DISPLAY_TABLE_ROW:
            layout_table_row_fixed(s, box);
            s->row++;
            break;
        case CSS_DISPLAY_TABLE_ROW_GROUP:
        case CSS_DISPLAY_TABLE_HEADER_GROUP:
        case CSS_DISPLAY_TABLE_FOOTER_GROUP:
            if (layout_table_fixed(s, box))
                return -1;
            break;
        case CSS_DISPLAY_TABLE_COLUMN_GROUP:
            if (layout_table_fixed(s, box))
                return -1;
            break;
        case CSS_DISPLAY_TABLE_COLUMN:
            if (++s->column_index > s->nb_cols)
                allocate_column(s);
            if (props->width != CSS_AUTO) {
                /* XXX: margins ? */
                s->cols[s->column_index - 1].width = 
                    max(s->cols[s->column_index - 1].width,
                        props->width);
                s->cols[s->column_index - 1].width_fixed = 1;
            }
            break;
        default:
            break;
        }
        box = box->next;
    }
    return 0;
}

static int layout_table_fixed1(TableLayout *tl, CSSBox *table_box)
{
    int nb_auto_cols, available_width, available_width1, i, j, tot_width;
    int cell_width;

    tl->nb_cols = 0;
    tl->nb_cols_allocated = 0;
    tl->cols = NULL;
    tl->column_index = 0;
    tl->row = 0;
    tl->table_width = table_box->props->width;
    if (layout_table_fixed(tl, table_box))
        return -1;
    
    /* compute the column widths */
    available_width = tl->table_width;
    available_width -= tl->border_h * (tl->nb_cols + 1);
    available_width1 = available_width;
    nb_auto_cols = 0;
    for(i=0;i<tl->nb_cols;i++) {
        if (tl->cols[i].width_fixed)
            available_width -= tl->cols[i].width;
        else
            nb_auto_cols++;
    }
    
    if (nb_auto_cols > 0) {
        cell_width = available_width / nb_auto_cols;
    } else {
        cell_width = 0;
    }

    tot_width = 0;
    for(i=0;i<tl->nb_cols;i++) {
        if (!tl->cols[i].width_fixed)
            tl->cols[i].width = cell_width;
        tot_width += tl->cols[i].width;
    }

    /* modify the widths so that their sum is exactly the table width */
    j = 0;
    while (tot_width < available_width1) {
        while (j < tl->nb_cols && tl->cols[j].width_fixed)
            j++;
        if (j == tl->nb_cols)
            break;
        tl->cols[j].width++;
        tot_width++;
        j++;
    }
    /* the table width is fixed */
    tl->min_width = tl->table_width;
    tl->max_width = tl->table_width;
    return 0;
}

/**************************************************************/
/* 'auto' layout algorithm */

static int layout_table_row_auto(TableLayout *s, CSSBox *row)
{
    CSSBox *cell, *cell1;
    CSSState *props;
    CSSRect border;
    int i, colspan, min_w, max_w, col, col1, w, fixed;
    ColStruct *c;

    col = 0;
    cell1 = row->u.child.first;
    for(;;) {
        /* XXX: factorize with rendering step */
        if (col < s->nb_cols)
            c = &s->cols[col];
        else
            c = NULL;

        if (c && c->row_span_left != 0) {
            cell = c->cell;
        } else {
            cell = cell1;
            if (!cell)
                break;
            cell1 = cell1->next;
            if (cell->props->display != CSS_DISPLAY_TABLE_CELL)
                continue;
        }
        props = cell->props;
        colspan = props->column_span;
        if (colspan < 1)
            colspan = 1;
        if (c && c->row_span_left != 0) {
            c->row_span_left--;
        } else {
            /* increase number of column if needed */
            col1 = col + colspan;
            for(i=s->nb_cols;i<col1;i++) 
                allocate_column(s);
            
            /* compute min & max size for the cell */
            if (css_layout_block_min_max(s->ctx, 
                                         &min_w, &max_w, cell) < 0)
                return -1;
            fixed = 1;
            if (props->width != CSS_AUTO) {
                min_w = max(props->width, min_w);
                max_w = max(props->width, max_w);
                fixed = 1;
            }
            /* take into account the padding & borders */
            if (s->border_collapse == CSS_BORDER_COLLAPSE_SEPARATE) {
                border = props->border;
            } else {
                border.x1 = div2rnd(props->border.x1, col);
                border.x2 = div2rnd(props->border.x2, col + 1);
            }
            w = props->padding.x1 + props->padding.x2 +
                border.x1 + border.x2;
            min_w += w;
            max_w += w;

            c = &s->cols[col];
            /* handle row span */
            if (props->row_span > 1) {
                c->row_span_left = props->row_span - 1;
                c->cell = cell;
            }

            /* XXX: margins ? */
            if (colspan == 1) {
                /* simple case: just increase widths */
                c->min_width = max(c->min_width, min_w);
                c->max_width = max(c->max_width, max_w);
                c->width_fixed = fixed;
            } else {
                int min_w1, max_w1, delta, d, r;
                
                /* increase min & max widths so that they are at least
                   larger enough for the cell */
                min_w1 = 0;
                max_w1 = 0;
                for(i=0;i<colspan;i++) {
                    c->width_fixed = fixed;
                    min_w1 += c->min_width;
                    max_w1 += c->max_width;
                    c++;
                }
                delta = min_w - min_w1;
                if (delta > 0) {
                    d = delta / colspan;
                    r = delta % colspan;
                    c = &s->cols[col];
                    for(i=0;i<colspan;i++) {
                        c->min_width += d;
                        if (i < r)
                            c->min_width++;
                        c++;
                    }
                }
                delta = max_w - max_w1;
                if (delta > 0) {
                    d = delta / colspan;
                    r = delta % colspan;
                    c = &s->cols[col];
                    for(i=0;i<colspan;i++) {
                        c->max_width += d;
                        if (i < r)
                            c->max_width++;
                        c++;
                    }
                }
            }
        }
        col += colspan;
    }
    return 0;
}

static int layout_table_auto(TableLayout *s, CSSBox *parent_box)
{
    CSSState *props;
    CSSBox *box;

    /* get the first row and examine each column element */
    box = parent_box->u.child.first;
    while (box != NULL) {
        props = box->props;
        switch(props->display) {
        case CSS_DISPLAY_TABLE_ROW:
            if (layout_table_row_auto(s, box) < 0)
                return -1;
            s->row++;
            break;
        case CSS_DISPLAY_TABLE_ROW_GROUP:
        case CSS_DISPLAY_TABLE_HEADER_GROUP:
        case CSS_DISPLAY_TABLE_FOOTER_GROUP:
            if (layout_table_auto(s, box))
                return -1;
            break;
        case CSS_DISPLAY_TABLE_COLUMN_GROUP:
            /* XXX: do it */
            break;
        case CSS_DISPLAY_TABLE_COLUMN:
            /* XXX: do it */
            break;
        default:
            break;
        }
        box = box->next;
    }
    return 0;
}

static int layout_table_auto1(TableLayout *s, CSSBox *table_box)
{
    int min_tw, max_tw, tw, i, delta, d, r;
    ColStruct *c;

    s->nb_cols = 0;
    s->nb_cols_allocated = 0;
    s->cols = NULL;
    s->column_index = 0;
    s->row = 0;
    if (layout_table_auto(s, table_box))
        return -1;
    
    /* now we have min_width and max_width for each column */

    /* compute the minimum and maximum table width */
    min_tw = s->border_h * (s->nb_cols + 1);
    max_tw = min_tw;
    for(i = 0, c = s->cols; i < s->nb_cols; i++, c++) {
        min_tw += c->min_width;
        max_tw += c->max_width;
    }

    if (s->compute_min_max) {
        if (table_box->props->width == CSS_AUTO) {
            s->min_width = min_tw;
            s->max_width = max_tw;
        } else {
            tw = max(min_tw, table_box->props->width);
            s->min_width = tw;
            s->max_width = tw;
        }
        return 0;
    }

    /* compute the table width */
    if (table_box->props->width == CSS_AUTO) {
        /* NOTE: in this case, table_box->width is the available width
           in the containing box */
        if (max_tw < table_box->width) {
            tw = max_tw;
        } else {
            tw = max(min_tw, table_box->width);
        }
    } else {
        /* fixed table width */
        tw = max(min_tw, table_box->props->width);
    }

    /* compute the cell widths from the computed table width */
    if (tw < max_tw) {
        for(i = 0, c = s->cols; i < s->nb_cols; i++, c++)
            c->width = c->min_width;
        delta = tw - min_tw;
    } else {
        for(i = 0, c = s->cols; i < s->nb_cols; i++, c++)
            c->width = c->max_width;
        delta = tw - max_tw;
    }
    if (delta > 0) {
        /* well, could do better (use max_width ?), but it works and it is
           fast */
        d = delta / s->nb_cols;
        r = delta % s->nb_cols;
        for(i = 0, c = s->cols; i < s->nb_cols; i++, c++) {
            c->width += d;
            if (i < r)
                c->width++;
        }
    }

    /* init again row span left, in case the row spans were incorrect */
    for(i = 0, c = s->cols; i < s->nb_cols; i++, c++)
        c->row_span_left = 0;
    
    s->table_width = tw;
    table_box->width = tw;
    return 0;
}

/**************************************************************/
/* table rendering */

/* we need this function because the default align is baseline */
static inline int is_valign_baseline(int v)
{
    return (v != CSS_VERTICAL_ALIGN_TOP &&
            v != CSS_VERTICAL_ALIGN_BOTTOM &&
            v != CSS_VERTICAL_ALIGN_MIDDLE);
}

static int render_table_row(TableLayout *s, 
                            CSSBox *row)
{
    LayoutOutput layout;
    int x, row_height, col, nb_cols, w, h, baseline, colspan, i;
    CSSBox *cell, *cell1;
    CSSState *props;
    CSSRect border;
    
    /* compute horizontal position of cells */
    x = 0;
    col = 0;
    baseline = 0;
    cell1 = row->u.child.first;
    col = 0; 
    for(;;) {
        ColStruct *c = &s->cols[col];
        
        /* compute which cell we examine */ 
        if (c->row_span_left != 0) {
            cell = c->cell;
        } else {
            cell = cell1;
            if (!cell)
                break;
            cell1 = cell1->next;
            if (cell->props->display != CSS_DISPLAY_TABLE_CELL)
                continue;
        }
        props = cell->props;

        colspan = props->column_span;
        if (colspan < 1)
            colspan = 1;
        if (col + colspan > s->nb_cols)
            colspan = s->nb_cols - col;
        if (colspan <= 0)
            break;
        
        if (c->row_span_left != 0) {
            c->row_span_left--;
            /* modify x to skip column */
            for(i=0;i<colspan;i++)
                x += s->border_h + s->cols[col + i].width;
        } else {
            c->cell = cell;
            if (s->border_collapse == CSS_BORDER_COLLAPSE_SEPARATE) {
                border = props->border;
            } else {
                border.x1 = div2rnd(props->border.x1, col);
                border.x2 = div2rnd(props->border.x2, col + 1);
                border.y1 = div2rnd(props->border.x1, s->row);
                border.y2 = div2rnd(props->border.x2, s->row + 1);
            }
                
            /* set the cell width */
            w = s->cols[col].width;
            for(i=1;i<colspan;i++)
                w += s->border_h + s->cols[col + i].width;
            
            cell->width = w - props->padding.x1 - props->padding.x2 -
                border.x1 - border.x2;
            if (css_layout_block(s->ctx, &layout, cell))
                return -1;

            c->height = cell->height + border.y1 + border.y2 + 
                props->padding.y1 + props->padding.y2;
            /* init row span counter */
            if (props->row_span > 1)
                c->row_span_left = props->row_span - 1;
            c->prev_row_height = 0;
            cell->x = x + s->border_h + border.x1 + props->padding.x1;
            x += w + s->border_h;
            /* align to top */
            cell->y = border.y1 + props->padding.y1;
            /* compute baseline of the row */
            c->vertical_align = props->vertical_align;
            if (is_valign_baseline(c->vertical_align)) {
                baseline = max(baseline, layout.baseline);
                c->baseline = layout.baseline;
            }
        }
        col += colspan;
    }
    nb_cols = col;

    /* now compute vertical position of cells if baseline and 
       compute row_height */
    row_height = 0;
    for(col = 0; col < nb_cols;) {
        ColStruct *c = &s->cols[col];
        cell = c->cell;
        colspan = cell->props->column_span;
        if (colspan < 1)
            colspan = 1;
        if (c->row_span_left == 0) {
            h = c->height;
            /* align to baseline if needed */
            if (h > 0 && is_valign_baseline(c->vertical_align)) {
                int delta = baseline - c->baseline;
                cell = c->cell;
                h += delta;
                cell->padding_top = delta;
                cell->y += delta;
            }
            if (cell->props->height != CSS_AUTO)
                h = max(h, cell->props->height);
            row_height = max(h - c->prev_row_height, row_height);
        }
        col += colspan;
    }
    if (row->props->height != CSS_AUTO) {
        row_height = max(row_height, row->props->height);
    }

    /* we know row_height: definitive vertical positionning */
    for(col = 0; col < nb_cols;) {
        ColStruct *c = &s->cols[col];
        int delta, cell_height;

        cell = c->cell;
        cell_height = c->prev_row_height + row_height;
        if (c->row_span_left == 0) {
            h = c->height;
            switch(c->vertical_align) {
            case CSS_VERTICAL_ALIGN_BOTTOM:
                delta = cell_height - c->height;
                cell->padding_top = delta;
                cell->y += delta;
                break;
            case CSS_VERTICAL_ALIGN_MIDDLE:
                delta = (cell_height - c->height) / 2;
                cell->padding_top = delta;
                cell->y += delta;
                break;
            default:
                break;
            }
            /* compute bottom padding so that we fill the row height */
            cell->padding_bottom = cell_height - (h + cell->padding_top);
        } else {
            c->prev_row_height = s->border_v + cell_height;
        }
        colspan = cell->props->column_span;
        if (colspan < 1)
            colspan = 1;
        col += colspan;
    }

    row->width = s->table_width;
    row->height = row_height + s->border_v;
    row->x = 0;
    row->y = s->y + s->border_v;
    s->y += row->height;
    return 0;
}

static int layout_table_render(TableLayout *s, CSSBox *parent_box)
{
    CSSState props1, *props = &props1;
    CSSBox *box;

    /* get the first row and examine each column element */
    box = parent_box->u.child.first;
    while (box != NULL) {
        props = box->props;
        switch(props->display) {
        case CSS_DISPLAY_TABLE_ROW:
            if (render_table_row(s, box))
                return -1;
            s->row++;
            break;
        case CSS_DISPLAY_TABLE_ROW_GROUP:
        case CSS_DISPLAY_TABLE_HEADER_GROUP:
        case CSS_DISPLAY_TABLE_FOOTER_GROUP:
            if (layout_table_render(s, box))
                return -1;
            break;
        case CSS_DISPLAY_TABLE_CAPTION:
            /* save first caption box */
            if (!s->caption_box)
                s->caption_box = box;
            break;
        case CSS_DISPLAY_TABLE_COLUMN_GROUP:
        case CSS_DISPLAY_TABLE_COLUMN:
        default:
            break;
        }
        box = box->next;
    }
    return 0;
}

/*
 * layout a table box. if min_max_compute is true, then only compute min/max info
 */
static int css_layout_table(CSSContext *s, LayoutOutput *table_layout,
                            CSSBox *table_box, int compute_min_max)
{
    TableLayout tl1, *tl = &tl1;
    CSSState *table_props = table_box->props;
    CSSBox *caption_box;

    tl->ctx = s;
    tl->border_collapse = table_props->border_collapse;
    tl->compute_min_max = compute_min_max;
    if (tl->border_collapse == CSS_BORDER_COLLAPSE_SEPARATE) {
        tl->border_h = table_props->border_spacing_horizontal;
        tl->border_v = table_props->border_spacing_vertical;
    } else {
        tl->border_h = 0;
        tl->border_v = 0;
    }
    table_layout->margin_top = table_props->margin.y1;
    table_layout->margin_bottom = table_props->margin.y2;

    if (table_props->table_layout == CSS_TABLE_LAYOUT_FIXED &&
        table_props->width != CSS_AUTO) {
        /* fixed algorithm */
        if (layout_table_fixed1(tl, table_box))
            goto fail;
    } else {
        /* fixed algorithm */
        if (layout_table_auto1(tl, table_box)) 
            goto fail;
    }
    
    if (tl->compute_min_max) {
        table_layout->min_width = tl->min_width;
        table_layout->max_width = tl->max_width;
        return 0;
    }


    /* now we are ready to layout the table ! */
    tl->row = 0;
    tl->y = 0;
    tl->caption_box = NULL;
    if (layout_table_render(tl, table_box)) {
    fail:
        free(tl->cols);
        return -1;
    }
    tl->y += tl->border_v;
    /* compute total table height */
    table_box->height = max(tl->y, table_box->height);

    free(tl->cols);

    /* handle table caption */
    caption_box = tl->caption_box;
    if (caption_box) {
        LayoutOutput caption_layout;
        int h;

        if (caption_box->props->width != CSS_AUTO) {
            caption_box->width = caption_box->props->width;
        } else if (caption_box->props->caption_side == CSS_CAPTION_SIDE_TOP ||
                   caption_box->props->caption_side == CSS_CAPTION_SIDE_BOTTOM) {
            caption_box->width = table_box->width;
        } else {
            /* use a default width */
            /* XXX: find a better guess !! */
            caption_box->width = 100;
        }
        if (caption_box->props->caption_side == CSS_CAPTION_SIDE_LEFT ||
            caption_box->props->caption_side == CSS_CAPTION_SIDE_RIGHT) {   
            caption_box->height = table_box->height;
        }
        if (css_layout_block(s, &caption_layout, caption_box))
            return -1;
        h = caption_box->height + 
            caption_box->props->border.y1 + caption_box->props->padding.y1 +
            caption_box->props->border.y2 + caption_box->props->padding.y2;
        /* put the caption in the table margin */
        /* XXX: potentially incorrect: table margins */
        switch(caption_box->props->caption_side) {
        case CSS_CAPTION_SIDE_TOP:
            caption_box->x = (table_box->width - caption_box->width) / 2;
            caption_box->y = -h;
            if (table_layout->margin_top < h)
                table_layout->margin_top = h;
            break;
        case CSS_CAPTION_SIDE_BOTTOM:
            caption_box->x = (table_box->width - caption_box->width) / 2;
            caption_box->y = table_box->height;
            if (table_layout->margin_bottom < h)
                table_layout->margin_bottom = h;
            break;
        case CSS_CAPTION_SIDE_RIGHT:
            caption_box->x = table_box->width;
            caption_box->y = 0;
            break;
        case CSS_CAPTION_SIDE_LEFT:
            caption_box->x = -caption_box->width;
            caption_box->y = 0;
            break;
        }
    }
    return 0;
}

/* add a float. Its layout is postponed until a start of line in an
   inline context is reached */
static int css_add_float(InlineLayout *s, CSSBox *box)
{
    FloatBlock *b, **pb;

    b = malloc(sizeof(FloatBlock));
    if (!b)
        return 0;
    b->box = box;
    b->float_type = -1;
    b->next = NULL;

    /* add the float at the end of the list */
    pb = &s->layout_state->first_float;
    while (*pb != NULL)
        pb = &(*pb)->next;
    *pb = b;
    return 0;
}

static void css_free_floats(FloatBlock *b)
{
    FloatBlock *b1;

    while (b != NULL) {
        b1 = b->next;
        free(b);
        b = b1;
    }
}

static int css_layout_block_recurse(LayoutState *s, LayoutOutput *block_layout,
                                    CSSBox *block_box, int x_parent, int y_parent);
static int css_layout_block_recurse1(InlineLayout *il, CSSBox *box, 
                                     int baseline);

static int css_layout_block_iterate(InlineLayout *il, CSSBox *box, int baseline)
{
    CSSBox *box1, *box2;
    int ret;

    for(box1 = box->u.child.first; box1 != NULL; box1 = box2) {
        box2 = box1->next; /* need to do that first because boxes may be split */
        ret = css_layout_block_recurse1(il, box1, baseline);
        if (ret)
            return ret;
    }
    return 0;
}

/* layout one box in an inline or block context */
static int css_layout_block_recurse1(InlineLayout *il, CSSBox *box, 
                                     int baseline)
{
    CSSState *props;
    int ret, ymargin;
    LayoutOutput layout;

    if (il->ctx->abort_func(il->ctx->abort_opaque))
        return -1;
    
    props = box->props;
    if (props->position == CSS_POSITION_ABSOLUTE ||
        props->position == CSS_POSITION_FIXED) {
        /* XXX: fixed is not handled */
        if (props->display != CSS_DISPLAY_NONE) {
            int min_w, w;
            if (props->width == CSS_AUTO) {
                css_layout_block_min_max(il->ctx, &min_w, &w,  box);
            } else {
                w = props->width;
            }
            box->width = w;
            box->height = 0;
            if (css_layout_block(il->ctx, &layout, box))
                return -1;
            if (props->left != CSS_AUTO)
                box->x = props->left;
            else if (props->right != CSS_AUTO)
                box->x = il->total_width - box->width - props->right;
            if (props->top != CSS_AUTO)
                box->y = props->top;
#if 0
            /* XXX: must put it in a list as float to have box height */
            else if (props->bottom != CSS_AUTO)
                box->y = props->bottom; 
#endif
        }
    } else if (props->block_float != CSS_FLOAT_NONE) {
        /* float formatting : display is ignored except if set to none */
        if (props->display != CSS_DISPLAY_NONE) {
            if (css_add_float(il, box))
                return -1;
        }
    } else {
        /* now do the layout, according to the display type */
        switch(props->display) {
        case CSS_DISPLAY_LIST_ITEM:
        case CSS_DISPLAY_BLOCK:
        case CSS_DISPLAY_TABLE:
            if (il->layout_type != LAYOUT_TYPE_BLOCK) {
                css_end_inline_layout(il);
                il->last_ymargin = 0;
            }
            il->marker_box = NULL; /* the marker was already positionned correctly */

            if (props->width == CSS_AUTO) {
                int w;
                w = props->padding.x1 + props->padding.x2 +
                    props->border.x1 + props->border.x2;
                if (props->margin.x1 != CSS_AUTO)
                    w += props->margin.x1;
                if (props->margin.x2 != CSS_AUTO)
                    w += props->margin.x2;
                box->width = il->total_width - w;
            } else {
                box->width = props->width;
            }

            /* position the box before so that we can pass x_parent and y_parent */
            if (props->margin.x1 == CSS_AUTO &&
                props->margin.x2 == CSS_AUTO) {
                int w;
                w = props->border.x1 + props->padding.x1 + 
                    box->width +
                    props->padding.x2 + props->border.x2;
                box->x = (il->total_width - w) / 2;
            } else if (props->direction == CSS_DIRECTION_LTR) {
                box->x = props->margin.x1 + props->border.x1 + props->padding.x1;
            } else {
                box->x = il->total_width - (props->margin.x2 + props->border.x2 + 
                                             props->padding.x2 + box->width);
            }
            /* XXX: compute y position there, but difficult to do
               because we do not have the complete margin info */

            if (props->height == CSS_AUTO) {
                box->height = 0; /* will be extended later */
            } else {
                box->height = props->height;
            }
            /* XXX: y_parent does not take into account margins ! */
            if (css_layout_block_recurse(il->layout_state, &layout, box,
                                         il->x0 + box->x, 
                                         il->y0 + il->y + 
                                         props->border.y1 + props->padding.y1))
                return -1;

            /* compute the margin */
            if (il->is_first_box) {
                il->margin_top = max(il->margin_top, layout.margin_top);
                ymargin = 0; /* the margin is taken into account by the parent block */
            } else {
                ymargin = max(il->last_ymargin, layout.margin_top);
            }
            il->last_ymargin = layout.margin_bottom;
            /* compute the box position */
            box->y = il->y + ymargin + props->border.y1 + props->padding.y1;
            box->padding_top = 0;
            box->padding_bottom = 0;
            /* update position for the next box */
            il->y = box->y + box->height + props->border.y2 + props->padding.y2;

            /* apply relative offset if specified */
            if (props->position == CSS_POSITION_RELATIVE) {
                if (props->left != CSS_AUTO)
                    box->x += props->left;
                else if (props->right != CSS_AUTO)
                    box->x -= props->right;
                if (props->top != CSS_AUTO)
                    box->y += props->top;
                else if (props->bottom != CSS_AUTO)
                    box->y -= props->bottom;
            }
            break;
        case CSS_DISPLAY_MARKER:
            /* marker is put in the left margin of block_box */
            {
                int min_w, w, offset;
                /* compute marker width */
                if (props->width == CSS_AUTO) {
                    css_layout_block_min_max(il->ctx, &min_w, &w,  box);
                } else {
                    w = props->width;
                }
                box->width = w;
                box->height = 0;

                if (css_layout_block(il->ctx, &layout, box))
                    return -1;
                /* put it in the margin */
                offset = props->marker_offset;
                if (offset == CSS_AUTO) {
                    if (il->ctx->media == CSS_MEDIA_TTY)
                        offset = 1;
                    else
                        offset = 8; /* XXX: add constant */
                }
                w = props->margin.x1 + props->border.x1 + props->padding.x1 +
                    w + 
                    props->margin.x2 + props->border.x2 + props->padding.x2;
                box->x -= (w + offset);
                /* margin is also taken into account */
                box->y = il->y + props->border.y1 + props->padding.y1 + layout.margin_top;
                /* note: the marker Y position can be modified if an
                   inline context comes just after */
                il->marker_box = box;
                il->marker_baseline = layout.baseline;
            }
            break;
        case CSS_DISPLAY_INLINE:
        case CSS_DISPLAY_INLINE_TABLE:
        case CSS_DISPLAY_INLINE_BLOCK:
            if (il->layout_type != LAYOUT_TYPE_INLINE)
                css_start_inline_layout(il);

            /* from this box, we are in an inline layout context */

            if (props->display != CSS_DISPLAY_INLINE_TABLE &&
                props->display != CSS_DISPLAY_INLINE_BLOCK &&
                box->content_type == CSS_CONTENT_TYPE_CHILDS) {
                css_layout_block_iterate(il, box, baseline);
            } else {
                ret = css_layout_inline_box(il, box, baseline);
                if (ret)
                    return ret;
            }
            break;
        default:
            dprintf("Unexpected display type : %d\n", props->display);
            break;
        case CSS_DISPLAY_NONE:
            break;
        }
    }
    il->is_first_box = 0;
    return 0;
}

/* layout the interior of box 'block_box'. 'block_box->width' and
   'block_box->height' must have reasonnable values before calling
   this function. If height == 0, then the box is extended as needed.
   x_parent and y_parent are the absolute coordinates of
   block_box. They are needed only in case of floats blocks.  */
static int css_layout_block_recurse(LayoutState *s, LayoutOutput *block_layout,
                                    CSSBox *block_box, int x_parent, int y_parent)
{
    CSSBox *box;
    CSSState *block_props;
    InlineLayout inline_layout, *il = &inline_layout;
    int ret;

    block_props = block_box->props;
#if 0
    {
        printf("layout %s", css_ident_str(block_box->tag));
        if (block_box->content_type == CSS_CONTENT_TYPE_BUFFER) 
            printf(" offset=%ld", block_box->u.buffer.start);
        printf("\n");
    }
#endif
    /* handle simple cases here */
    if (block_props->display == CSS_DISPLAY_TABLE ||
        block_props->display == CSS_DISPLAY_INLINE_TABLE) {
        if (block_props->height == CSS_AUTO) {
            block_box->height = 0; /* will be extended later */
        } else {
            block_box->height = block_props->height;
        }
        if (css_layout_table(s->ctx, block_layout, block_box, 0))
            return -1;
        return 0;
    }
    if (block_box->content_type == CSS_CONTENT_TYPE_IMAGE) {
        /* nothing more to do */
        return 0;
    }

    /* put initial values for margins */
    block_layout->margin_top = block_props->margin.y1;
    block_layout->margin_bottom = block_props->margin.y2;

    box = block_box->u.child.first;
    if (!box) {
        /* empty block, cannot do more */
        if (block_props->height != CSS_AUTO)
            block_box->height = block_props->height;
        return 0;
    }
    /* a subset of the inline layout is also used for the block layout */
    il->ctx = s->ctx;
    il->compute_min_max = 0;
    il->layout_state = s;
    il->y = 0;
    il->x0 = x_parent;
    il->y0 = y_parent;
    il->total_width = block_box->width;
    il->last_ymargin = 0; /* not used */
    il->is_first_box = 1;
    il->margin_top = block_layout->margin_top;
    il->marker_box = NULL;
    il->layout_type = LAYOUT_TYPE_BLOCK;
    il->first_line_baseline = 0;
    il->line_count = 0;
    
    ret = css_layout_block_iterate(il, block_box, 0);
    if (ret)
        return ret;

    /* start block layout to flush last line */
    if (il->layout_type != LAYOUT_TYPE_BLOCK)
        css_end_inline_layout(il);

    /* update the bottom margin of the whole block */
    block_layout->margin_top = il->margin_top;
    block_layout->margin_bottom = max(block_layout->margin_bottom, 
                                      il->last_ymargin);
    block_layout->baseline = il->first_line_baseline;
    /* update the block height if necessary (XXX: incorrect if not
       auto) */
    if (il->y > block_box->height)
        block_box->height = il->y;
    return 0;
}

/* layout the interior of box 'block_box'. 'block_box->width' and
   'block_box->height' must have reasonnable values before calling
   this function. If height == 0, then the box is extended as needed.
   no floating boxes are initially registered.
   */
static int css_layout_block(CSSContext *s, LayoutOutput *block_layout,
                            CSSBox *block_box)
{
    LayoutState layout_state;
    int ret;

    layout_state.ctx = s;
    layout_state.first_float = NULL;
    ret = css_layout_block_recurse(&layout_state, block_layout, block_box, 0, 0);
 
    css_free_floats(layout_state.first_float);

    return ret;
}

/* min/max layout */
static int css_layout_box_min_max(InlineLayout *il, CSSBox *box)
{
    CSSState *props = box->props;
    int w, min_w1, max_w1, ret;

    if (il->ctx->abort_func(il->ctx->abort_opaque))
        return -1;

    if (props->position == CSS_POSITION_ABSOLUTE ||
        props->position == CSS_POSITION_FIXED) {
        /* ignore this box */
    } else if (props->block_float != CSS_FLOAT_NONE) {
        /* floating boxes : we ensure that we can put them on a line */
        if (props->display != CSS_DISPLAY_NONE) {
            if (props->width != CSS_AUTO) {
                min_w1 = props->width;
                max_w1 = props->width;
            } else {
                css_layout_block_min_max(il->ctx, &min_w1, &max_w1, box);
            }
            il->min_width = max(il->min_width, min_w1);
            /* XXX: add to max width ? */
            il->max_width = max(il->max_width, max_w1);
        }
    } else {
        switch(props->display) {
        case CSS_DISPLAY_LIST_ITEM:
        case CSS_DISPLAY_BLOCK:
        case CSS_DISPLAY_TABLE:
            if (il->layout_type != LAYOUT_TYPE_BLOCK)
                css_end_inline_layout(il);

            if (props->width != CSS_AUTO && 
                props->display != CSS_DISPLAY_TABLE) {
                min_w1 = props->width;
                max_w1 = props->width;
            } else {
                /* look at the block inside */
                css_layout_block_min_max(il->ctx, &min_w1, &max_w1, box);
            }
            w = props->padding.x1 + props->padding.x2 +
                props->border.x1 + props->border.x2;
            if (props->margin.x1 != CSS_AUTO)
                w += props->margin.x1;
            if (props->margin.x2 != CSS_AUTO)
                w += props->margin.x2;
            il->min_width = max(il->min_width, min_w1 + w);
            il->max_width = max(il->max_width, max_w1 + w);
            break;
        case CSS_DISPLAY_INLINE:
        case CSS_DISPLAY_INLINE_TABLE:
        case CSS_DISPLAY_INLINE_BLOCK:
            if (il->layout_type != LAYOUT_TYPE_INLINE)
                css_start_inline_layout(il);
            
            if (props->display != CSS_DISPLAY_INLINE_TABLE &&
                props->display != CSS_DISPLAY_INLINE_BLOCK &&
                box->content_type == CSS_CONTENT_TYPE_CHILDS) {
                CSSBox *box1, *box2;
                for(box1 = box->u.child.first; box1 != NULL; box1 = box2) {
                    box2 = box1->next; /* need to do that first because boxes may be split */
                    ret = css_layout_box_min_max(il, box1);
                    if (ret)
                        return ret;
                }
            } else {
                ret = css_layout_inline_box(il, box, 0);
                if (ret)
                    return ret;
            }
            break;
        }
    }
    return 0;
}

/* compute the minimum and maximum width of the content of a block */
static int css_layout_block_min_max(CSSContext *s, 
                                    int *min_width_ptr, int *max_width_ptr,
                                    CSSBox *block_box)
{
    CSSBox *box, *box1;
    LayoutOutput layout;
    InlineLayout inline_layout, *il = &inline_layout;
    int ret;

    if (block_box->props->display == CSS_DISPLAY_TABLE ||
        block_box->props->display == CSS_DISPLAY_INLINE_TABLE) {
        if (css_layout_table(s, &layout, block_box, 1))
            return -1;
        *min_width_ptr = layout.min_width;
        *max_width_ptr = layout.max_width;
        return 0;
    }

    il->ctx = s;
    il->compute_min_max = 1;
    il->min_width = 0;
    il->max_width = 0;
    il->layout_type = LAYOUT_TYPE_BLOCK;

    box = block_box->u.child.first;
    if (!box) {
        /* empty block, cannot do more */
        if (block_box->props->width != CSS_AUTO)
            il->min_width = il->max_width = block_box->props->width;
    } else {
        for(;box != NULL; box = box1) {
            box1 = box->next;
            ret = css_layout_box_min_max(il, box);
            if (ret)
                return ret;
        }
    }
    /* start block layout to flush last line */
    if (il->layout_type != LAYOUT_TYPE_BLOCK)
        css_end_inline_layout(il);

    *min_width_ptr = il->min_width;
    *max_width_ptr = il->max_width;
    return 0;
}

/* bounding box extraction. get document extends & global background
   infos. Also translate all relative coordinates into absolute
   ones. XXX: use absolute coordinates in the whole layout. */
static void css_compute_bbox_block(CSSContext *s,
                                   CSSBox *box, int x_parent, int y_parent)
{
    CSSBox *tt;
    CSSState *props = box->props;
    int x0, y0;
    
    if (props->visibility == CSS_VISIBILITY_HIDDEN) {
        css_set_rect(&box->bbox, 0, 0, 0, 0);
        return;
    }
    x0 = box->x;
    y0 = box->y;
    /* convert to absolute position if needed */
    if (!box->absolute_pos) {
        x0 += x_parent;
        y0 += y_parent;
        box->x = x0;
        box->y = y0;
    }
    
    /* update bounding box */
    css_set_rect(&box->bbox,
                 x0 - (props->padding.x1 + props->border.x1),
                 y0 - (props->padding.y1 + box->padding_top + 
                       props->border.y1),
                 x0 + box->width + props->padding.x2 + props->border.x2,
                 y0 + box->height + 
                 (props->padding.y2 + box->padding_bottom + props->border.y2));

    /* now display the content ! */
    if (box->content_type == CSS_CONTENT_TYPE_CHILDS) {
        /* other boxes are inside: display them */
        tt = box->u.child.first;
        while (tt) {
            css_compute_bbox_block(s, tt, x0, y0);
            css_union_rect(&box->bbox, &tt->bbox);
            tt = tt->next;
        }
    }
}

/* main css layout function. Return non zero if interrupted */
int css_layout(CSSContext *s, CSSBox *box, int width,
               CSSAbortFunc abort_func, void *abort_opaque)
{
    LayoutOutput layout;
    int ret;

    s->abort_func = abort_func;
    s->abort_opaque = abort_opaque;

    /* bidi compute */
    ret = css_layout_bidir_block(s, box);
    if (ret)
        return ret;

    /* layout */
    box->width = width;
    ret = css_layout_block(s, &layout, box);
    if (ret)
        return ret;

    //    css_dump(box);

    /* compute the bbox of all non hidden boxes */
    css_compute_bbox_block(s, box, 0, 0);
    return 0;
}

/* display utils */

#define MAX_LINE_SIZE 256

int box_get_text(CSSContext *s,
                 unsigned int *line_buf, int max_size, 
                 int *offsets, CSSBox *box)
{
    /* final box with text inside */
    unsigned long offset, offset0;
    unsigned int *q;
    int c, space_collapse, last_space, space;
    NextCharFunc nextc;
    CSSState *props = box->props;

    nextc = get_nextc(box);
    space_collapse = (props->white_space == CSS_WHITE_SPACE_NORMAL ||
                      props->white_space == CSS_WHITE_SPACE_NOWRAP);
    q = line_buf;
    offset = box->u.buffer.start;
    last_space = box->last_space;
    while (offset < box->u.buffer.end) {
        offset0 = offset;
        c = nextc(s->b, &offset);
        if (c == CSS_CONTENT_EOL)
            continue;
        space = css_is_space(c);
        if (space_collapse) {
            if (last_space && space)
                continue;
            last_space = space;
        }
        if (space)
            c = ' ';
        if ((q - line_buf) < max_size) {
            if (offsets)
                offsets[q - line_buf] = offset0;
            *q++ = c;
        }
    }
    return q - line_buf;
}

#define BFRAC 16

/* draw nice borders */
static void draw_borders(QEditScreen *scr,
                         int x1, int y1, int x2, int y2, CSSState *props)
{
    int dir, i, t1, t2, u1, v1, u2, v2, u1incr, u2incr, style, w, v;
    unsigned int color, color1, color2;

    for(dir=0;dir<4;dir++) {
        style = props->border_styles[dir];
        if (style == CSS_BORDER_STYLE_NONE ||
            style == CSS_BORDER_STYLE_HIDDEN)
            continue;
        color1 = props->border_colors[dir];
        if (style == CSS_BORDER_STYLE_GROOVE ||
            style == CSS_BORDER_STYLE_RIDGE ||
            style == CSS_BORDER_STYLE_INSET ||
            style == CSS_BORDER_STYLE_OUTSET) {
            /* XXX: better formula ? use luminance ? */
            unsigned int r, g, b, a;
            a = (color1 >> 24) & 0xff;
            r = (color1 >> 16) & 0xff;
            g = (color1 >> 8) & 0xff;
            b = (color1) & 0xff;
            
            r = min(r + 128, 255);
            g = min(g + 128, 255);
            b = min(b + 128, 255);

            color2 = (a << 24) | (r << 16) | (g << 8) | b;
            if ((style == CSS_BORDER_STYLE_INSET && dir >= 2) ||
                (style == CSS_BORDER_STYLE_OUTSET && dir < 2))
                color1 = color2;
        } else {
            color2 = color1;
        }

        w = ((int *)&props->border)[dir];
        if (w <= 0)
            continue;
        switch(dir) {
        case 2:
        case 0:
            u1 = y1;
            u2 = y2;
            v1 = y1 - props->border.y1;
            v2 = y2 + props->border.y2;
            break;
        default:
            u1 = x1;
            u2 = x2;
            v1 = x1 - props->border.x1;
            v2 = x2 + props->border.x2;
            break;
        }
        u1incr = ((v1 - u1) << BFRAC) / w;
        u2incr = ((v2 - u2) << BFRAC) / w;
        u1 = u1 << BFRAC;
        u2 = u2 << BFRAC;

        for(i=0;i<w;i++) {
            u1 += u1incr;
            u2 += u2incr;
            switch(style) {
            case CSS_BORDER_STYLE_DASHED: /* not handled */
            case CSS_BORDER_STYLE_DOTTED: /* not handled */
            case CSS_BORDER_STYLE_SOLID:
            case CSS_BORDER_STYLE_OUTSET:
            case CSS_BORDER_STYLE_INSET:
                color = color1;
                break;
            case CSS_BORDER_STYLE_DOUBLE:
                if (w <= 1 || ((i * 3) / w) != 1) {
                    color = color1;
                } else {
                    continue;
                }
                break;
            case CSS_BORDER_STYLE_RIDGE:
                v = 0;
                goto do_groove;
            case CSS_BORDER_STYLE_GROOVE:
                v = 1;
            do_groove:
                if (dir >= 2)
                    v = 1 - v;
                if (w > 1 && ((i * 2) / w) == v) {
                    color = color1;
                } else {
                    color = color2;
                }
                break;
            default:
                continue;
            }
            t1 = u1 >> BFRAC;
            t2 = u2 >> BFRAC;
            switch(dir) {
            case 0:
                fill_rectangle(scr, x1 - i - 1, t1, 1, t2 - t1, color);
                break;
            case 1:
                fill_rectangle(scr, t1, y1 - i - 1, t2 - t1, 1, color);
                break;
            case 2:
                fill_rectangle(scr, x2 + i, t1, 1, t2 - t1, color);
                break;
            default:
                fill_rectangle(scr, t1, y2 + i, t2 - t1, 1, color);
                break;
            }
        }
    }
}


static void box_display_text(CSSContext *s, CSSBox *box, int x0, int y0)
{
    CSSState *props = box->props;
    QEditScreen *scr = s->screen;
    unsigned int line_buf[MAX_LINE_SIZE];
    unsigned int glyphs[MAX_LINE_SIZE];
    int offsets[MAX_LINE_SIZE + 1], *offsets_ptr;
    unsigned int char_to_glyph_pos[MAX_LINE_SIZE], *ctg_ptr;
    int len, x, i, p, offset0, len1, w;
    QEFont *font;
    CSSColor color;

    if 
#if 0
        (s->selection_end > s->selection_start &&
        box->content_type == CSS_CONTENT_TYPE_BUFFER &&
        !(box->u.buffer.end <= s->selection_start ||
          box->u.buffer.start >= s->selection_end)) 
#else
            (box->content_type == CSS_CONTENT_TYPE_BUFFER)
#endif
    {
        /* more complicated selection highlighting */
        offsets_ptr = offsets;
        ctg_ptr = char_to_glyph_pos;
    } else {
        offsets_ptr = NULL;
        ctg_ptr = NULL;
    }
        
    len1 = box_get_text(s, line_buf, MAX_LINE_SIZE, offsets_ptr, box);
    
    len = unicode_to_glyphs(glyphs, ctg_ptr, MAX_LINE_SIZE, line_buf, len1, 
                            box->embedding_level & 1);
    if (len > 0) {
        font = css_select_font(scr, props);
        if (!offsets_ptr) {
            /* fast code */
            draw_text(scr, font,
                      x0, y0 + box->ascent, glyphs, len, props->color);
        } else {
            /* slower and more complicated code for selection handling */
            offsets[len1] = box->u.buffer.end;
            x = x0;
            for(i=0;i<len;i++) {
                p = char_to_glyph_pos[i];
                offset0 = offsets[p];
                w = glyph_width(scr, font, glyphs[i]);
                if (offset0 >= s->selection_start &&
                    offset0 < s->selection_end) {
                    color = s->selection_bgcolor;
                    fill_rectangle(scr, x, y0, 
                                   w, font->ascent + font->descent, color);
                }
                x += w;
            }

            x = x0;
            for(i=0;i<len;i++) {
                p = char_to_glyph_pos[i];
                offset0 = offsets[p];
                if (offset0 >= s->selection_start &&
                    offset0 < s->selection_end)
                    color = s->selection_fgcolor;
                else
                    color = props->color;
                draw_text(scr, font,
                          x, y0 + box->ascent, glyphs + i, 1, color);
                w = glyph_width(scr, font, glyphs[i]);
                x += w;
            }
        }
    }
}

/* display an image, currently, only show a border and the alt string */
#define ALT_TEXT_PADDING 3

static void box_display_image(CSSContext *s, CSSBox *box, int x0, int y0)
{
    QEditScreen *scr = s->screen;
    CSSState *props = box->props;
    CSSState img_props;
    QEFont *font;
    int i, len;
    unsigned int ubuf[256];
    
    if (s->media != CSS_MEDIA_TTY) {
        /* draw something inside the image content box, but
           only if the image is big enough (avoid displaying
           spacers) */
        if (box->width > 2 && box->height > 2) {
            img_props.border.x1 = 1;
            img_props.border.y1 = 1;
            img_props.border.x2 = 1;
            img_props.border.y2 = 1;
            for(i=0;i<4;i++) {
                img_props.border_colors[i] = QERGB(0, 0, 0);
                        img_props.border_styles[i] = CSS_BORDER_STYLE_INSET;
            }
            draw_borders(scr, x0 + 1, y0 + 1, 
                         x0 + box->width - 1, 
                         y0 + box->height - 1, &img_props);
            /* display the optional alt text */
            if (box->u.image.content_alt) {
                font = css_select_font(scr, props);
                len = utf8_to_unicode(ubuf, sizeof(ubuf) / sizeof(ubuf[0]), 
                                      box->u.image.content_alt);
                /* XXX: unicode, etc... */
                draw_text(scr, font,
                          x0 + ALT_TEXT_PADDING, 
                          y0 + font->ascent + ALT_TEXT_PADDING, 
                          ubuf, len, props->color);
            }
        }
    }
}

static void css_display_block(CSSContext *s, 
                              CSSBox *box, CSSState *props_parent,
                              CSSRect *clip_box, int dx, int dy)
{
    CSSState *props;
    int x0, y0, x1, y1, x2, y2;
    CSSBox *tt;
    QEditScreen *scr = s->screen;
    CSSRect old_clip;

    /* eval the properties for this box */
    props = box->props;

    if (props->display == CSS_DISPLAY_NONE)
        return;

    /* XXX: if hidden, display childs ? */
    if (props->visibility == CSS_VISIBILITY_HIDDEN)
        return;

    /* fast clip test */
    if (!css_is_inter_rect(&box->bbox, clip_box))
        return;

    x0 = box->x;
    y0 = box->y;
    x0 += dx;
    y0 += dy;

    x1 = x0 - props->padding.x1;
    y1 = y0 - (props->padding.y1 + box->padding_top);
    x2 = x0 + box->width + props->padding.x2;
    y2 = y0 + box->height + (props->padding.y2 + box->padding_bottom);

    /* background + padding */
    /* XXX: hack to exclude HTML tag from these computations */
    if (!s->bg_drawn && box->tag != CSS_ID_html) {
        int color;
        /* if no background drawn, we MUST draw one now with either
           the specified color or the default color */
        color = props->bgcolor;
        if (color == COLOR_TRANSPARENT)
            color = s->default_bgcolor;
        fill_rectangle(scr, s->bg_rect.x1, s->bg_rect.y1, 
                       s->bg_rect.x2 - s->bg_rect.x1, 
                       s->bg_rect.y2 - s->bg_rect.y1, color);
        s->bg_drawn = 1;
    } else if (props->bgcolor != COLOR_TRANSPARENT) {
        fill_rectangle(scr, x1, y1, x2 - x1, y2 - y1, props->bgcolor);
    }

    /* borders */
    if (s->media != CSS_MEDIA_TTY)
        draw_borders(scr, x1, y1, x2, y2, props);

    if (props->overflow == CSS_OVERFLOW_HIDDEN) {
        CSSRect r;
        r.x1 = x0;
        r.y1 = y0;
        r.x2 = x0 + box->width;
        r.y2 = y0 + box->height;
        push_clip_rectangle(scr, &old_clip, &r);
    }

    /* now display the content ! */
    switch(box->content_type) {
    case CSS_CONTENT_TYPE_IMAGE:
        box_display_image(s, box, x0, y0);
        break;
    case CSS_CONTENT_TYPE_CHILDS:
        /* other boxes are inside: display them */
        tt = box->u.child.first;
        while (tt) {
            css_display_block(s, tt, props, clip_box, dx, dy);
            tt = tt->next;
        }
        break;
    default:
        /* final box with text inside */
        box_display_text(s, box, x0, y0);
        break;
    }

    if (props->overflow == CSS_OVERFLOW_HIDDEN) {
        set_clip_rectangle(scr, &old_clip);
    }
#if 0
    {
        CSSColor color = 0xffffff;
        fill_rectangle(scr, x1, y1, x2 - x1, 1, color);
        fill_rectangle(scr, x1, y2, x2 - x1, 1, color);
        fill_rectangle(scr, x1, y1, 1, y2 - y1, color);
        fill_rectangle(scr, x2, y1, 1, y2 - y1, color);
    }
#endif    
}

/* dx & dy are added to each coordinates to do scrolling. 'clip_box'
   is a hint to optimize drawing */
void css_display(CSSContext *s, CSSBox *box, 
                 CSSRect *clip_box, int dx, int dy)
{
    CSSState props1, *default_props = &props1;
    CSSRect clip1;

    /* express the clip box in document coordinates because it is
       tested against the bounding boxes */
    css_set_rect(&clip1, 
                 clip_box->x1 - dx, clip_box->y1 - dy,
                 clip_box->x2 - dx, clip_box->y2 - dy);
    set_default_props(s, default_props);
    s->bg_rect = *clip_box;
    s->bg_drawn = 0;
    css_display_block(s, box, default_props, &clip1, dx, dy);

    /* if no background rectangle drawn, then draw it now with default color */
    if (!s->bg_drawn) {
        fill_rectangle(s->screen, s->bg_rect.x1, s->bg_rect.y1, 
                       s->bg_rect.x2 - s->bg_rect.x1, 
                       s->bg_rect.y2 - s->bg_rect.y1, 
                       s->default_bgcolor);
    }
}

/* get the cursor position inside a box */
typedef struct CSSCursorState {
    CSSContext *ctx;
    CSSRect cursor_pos;
    CSSBox *box;
    int x0, y0;
    int dirc;
    int offset;
} CSSCursorState;

static int css_get_cursor_func(void *opaque, 
                               CSSBox *box, int x0, int y0)
{
    CSSCursorState *s = opaque;
    unsigned int line_buf[MAX_LINE_SIZE];
    unsigned int glyphs[MAX_LINE_SIZE];
    int offsets[MAX_LINE_SIZE+1];
    unsigned int char_to_glyph_pos[MAX_LINE_SIZE];
    int posc, x, i, len, w, eol;
    QEditScreen *scr = s->ctx->screen;
    QEFont *font;
    CSSState *props;

    props = box->props;
    /* XXX: cannot put cursor in empty box */
    if (box->height == 0 || box->content_type != CSS_CONTENT_TYPE_BUFFER)
        return 0;

    eol = (box->content_eol != 0);
    if (!(s->offset >= box->u.buffer.start &&
          s->offset < (box->u.buffer.end + eol)))
            return 0;

    /* special case for eol */
    if (s->offset == box->u.buffer.end) {
        /* get eol width */
        font = css_select_font(s->ctx->screen, props);
        w = glyph_width(s->ctx->screen, font, '$');
        if (box->embedding_level & 1) {
            /* right to left : cursor on the left */
            x = -w;
        } else {
            x = box->width;
        }
        goto found;
    }
    
    /* get the text and get the cursor position */
    len = box_get_text(s->ctx, line_buf, MAX_LINE_SIZE, offsets, box);
    offsets[len] = box->u.buffer.end;
    
    for(i=0;i<len;i++) {
        if (s->offset >= offsets[i] && s->offset < offsets[i + 1]) {
            posc = i;
            goto found1;
        }
    }
    /* should never happen */
    return 0;
 found1:
    len = unicode_to_glyphs(glyphs, char_to_glyph_pos, MAX_LINE_SIZE, 
                            line_buf, len, box->embedding_level & 1);
    posc = char_to_glyph_pos[posc];
    
    font = css_select_font(scr, props);
    
    x = 0;
    for(i=0;i<posc;i++) {
        w = glyph_width(scr, font, glyphs[i]);
        x += w;
    }
    w = glyph_width(scr, font, glyphs[i]);
    
    /* cursor found : give its position */
 found:
    s->box = box;
    s->x0 = x0;
    s->y0 = y0;
    s->cursor_pos.x1 = x0 + x;
    s->cursor_pos.y1 = y0;
    s->cursor_pos.x2 = x0 + x + w;
    s->cursor_pos.y2 = y0 + box->height;
    s->dirc = box->embedding_level & 1;
    return 1;
}

int css_get_cursor_pos(CSSContext *s, CSSBox *box, 
                       CSSBox **box_ptr, int *x0_ptr, int *y0_ptr,
                       CSSRect *cursor_ptr, int *dir_ptr, 
                       int offset)
{
    CSSCursorState cursor_state;

    cursor_state.ctx = s;
    cursor_state.offset = offset;
    if (css_box_iterate(s, box, &cursor_state, css_get_cursor_func)) {
        *cursor_ptr = cursor_state.cursor_pos;
        *dir_ptr = cursor_state.dirc;
        if (box_ptr)
            *box_ptr = cursor_state.box;
        if (x0_ptr)
            *x0_ptr = cursor_state.x0;
        if (y0_ptr)
            *y0_ptr = cursor_state.y0;
        return 1;
    } else {
        return 0;
    }
}

int css_box_iterate(CSSContext *s, 
                    CSSBox *box,
                    void *opaque, CSSIterateFunc iterate_func)
{
    CSSBox *tt;

    if (box->content_type == CSS_CONTENT_TYPE_CHILDS) {
        tt = box->u.child.first;
        while (tt) {
            if (css_box_iterate(s, tt, opaque, iterate_func))
                return 1;
            tt = tt->next;
        }
    } else if (box->content_type == CSS_CONTENT_TYPE_BUFFER) {
        return iterate_func(opaque, box, box->x, box->y);
    }
    return 0;
}

/* return the offset of the closest char of x position.  */
int css_get_offset_pos(CSSContext *s, CSSBox *box, int xc, int dir)
{
    unsigned int line_buf[MAX_LINE_SIZE];
    unsigned int glyphs[MAX_LINE_SIZE];
    int offsets[MAX_LINE_SIZE];
    unsigned int char_to_glyph_pos[MAX_LINE_SIZE];
    int x, d, dmin, posc, len, i, w;
    QEFont *font;
    CSSState *props;

    assert (box->content_type == CSS_CONTENT_TYPE_BUFFER);
    props = box->props;

    /* get the text and get the cursor position */
    len = box_get_text(s, line_buf, MAX_LINE_SIZE, offsets, box);
    len = unicode_to_glyphs(glyphs, char_to_glyph_pos, MAX_LINE_SIZE, 
                            line_buf, len, box->embedding_level & 1);

    font = css_select_font(s->screen, props);

    dmin = MAXINT;
    x = 0;
    posc = -1;
    for(i=0;i<len;i++) {
        if (dir == 0 ||
            (dir > 0 && x > xc) ||
            (dir < 0 && x < xc)) {
            d = abs(x - xc);
            if (d < dmin) {
                dmin = d;
                posc = i;
            }
        }
        w = glyph_width(s->screen, font, glyphs[i]);
        x += w;
    }
    /* eol handling */
    if (box->content_eol) {
        /* if right to left, take it into account */
        if (box->embedding_level & 1) {
            w = glyph_width(s->screen, font, '$');
            x = -w;
        }
        if (dir == 0 ||
            (dir > 0 && x > xc) ||
            (dir < 0 && x < xc)) {
            d = abs(x - xc);
            if (d < dmin) {
                return box->u.buffer.end;
            }
        }
    }
    /* no matching position found */
    if (posc < 0)
        return -1;

    /* compute offset */
    for(i=0;i<len;i++) {
        if (posc == char_to_glyph_pos[i]) {
            return offsets[i];
        }
    }
    return -1;
}

/* for debugging */
#if defined(DEBUG)

void css_dump_box(CSSBox *box, int level)
{
    int i;
    CSSBox *b;
    const char *tag;

    for(i=0;i<level;i++) 
        printf(" ");
    if (box->tag == CSS_ID_NIL) {
        tag = "anon";
    } else {
        tag = css_ident_str(box->tag);
    }
    printf("<%s x=%d y=%d w=%d h=%d el=%d>\n", 
           tag, box->x, box->y, box->width, box->height, box->embedding_level);

    switch(box->content_type) {
    case CSS_CONTENT_TYPE_CHILDS:
        b = box->u.child.first;
        while (b != NULL) {
            css_dump_box(b, level + 1);
            b = b->next;
        }
        break;
    case CSS_CONTENT_TYPE_BUFFER:
        for(i=0;i<level + 1;i++) 
            printf(" ");
        printf("[offs=%lu %lu]\n",
               box->u.buffer.start, box->u.buffer.end);
        break;
    case CSS_CONTENT_TYPE_STRING:
        {
            unsigned long ptr;
            for(i=0;i<level + 1;i++) 
                printf(" ");
            printf("'");
            for(ptr=box->u.buffer.start; ptr < box->u.buffer.end; ptr++)
                printf("%c", *(unsigned char *)ptr);
            printf("'\n");
        }
        break;
    case CSS_CONTENT_TYPE_IMAGE:
        printf("[IMAGE]\n");
        break;
    }
    for(i=0;i<level;i++) 
        printf(" ");
    printf("</%s>\n", tag);
}

void css_dump(CSSBox *box)
{
    css_dump_box(box, 0);
}
#endif

/* box handling API */

/* create a new box.
   WARNING: the tag and the attributes are freed when the box is deleted 
 */
CSSBox *css_new_box(CSSIdent tag, CSSAttribute *attrs)
{
    CSSBox *box;

    box = malloc(sizeof(CSSBox));
    if (!box)
        return NULL;
    memset(box, 0, sizeof(CSSBox));
    box->tag = tag;
    box->attrs = attrs;
    return box;
}

CSSBox *css_add_box(CSSBox *parent_box, CSSBox *box)
{
    if (parent_box->content_type != CSS_CONTENT_TYPE_CHILDS) {
        dprintf("error: adding to a terminal box\n");
        return NULL;
    }
    if (!parent_box->u.child.first)
        parent_box->u.child.first = box;
    else
        parent_box->u.child.last->next = box;
    parent_box->u.child.last = box;
    box->parent = parent_box;
    return box;
}

/* delete a box and all boxes after and inside */
/* XXX: free generated content ! */
void css_delete_box(CSSBox *box)
{
    CSSBox *box1;
    CSSAttribute *a1, *a;
    CSSProperty *p1, *p;

    while (box != NULL) {
        switch(box->content_type) {
        case CSS_CONTENT_TYPE_CHILDS:
            css_delete_box(box->u.child.first);
            break;
        case CSS_CONTENT_TYPE_STRING:
            /* split boxes never own their content */
            if (!box->split)
                free((void *)box->u.buffer.start);
            break;
        case CSS_CONTENT_TYPE_IMAGE:
            free(box->u.image.content_alt);
            break;
        }
        box1 = box->next;
        a = box->attrs;
        while (a != NULL) {
            a1 = a->next;
            free(a);
            a = a1;
        }
        p = box->properties;
        while (p != NULL) {
            p1 = p->next;
            free(p);
            p = p1;
        }
        free(box);
        box = box1;
    }
}

void css_set_text_buffer(CSSBox *box,
                         int offset1, int offset2, int eol)
{
    box->content_type = CSS_CONTENT_TYPE_BUFFER;
    box->content_eol = eol;
    box->u.buffer.start = offset1;
    box->u.buffer.end = offset2;
}

/* note: the string is reallocated */
void css_set_text_string(CSSBox *box, const char *string)
{
    int len;
    char *str;

    box->content_type = CSS_CONTENT_TYPE_STRING;
    str = strdup(string);
    len = strlen(string);
    box->u.buffer.start = (unsigned long)str;
    box->u.buffer.end = (unsigned long)str + len;
}

void css_set_child_box(CSSBox *parent_box, CSSBox *box)
{
    parent_box->content_type = CSS_CONTENT_TYPE_CHILDS;
    parent_box->u.child.first = box;
    parent_box->u.child.last = box;
    box->parent = parent_box;
}

/* if 'box' is not a box suitable to have child, transform it into a
   such a box */
void css_make_child_box(CSSBox *box)
{
    CSSBox *box1;

    if (box->content_type != CSS_CONTENT_TYPE_CHILDS) {
        /* the box already contains text : we create a subbox cloning the parent box */
        box1 = css_new_box(CSS_ID_NIL, NULL);
        box1->u.buffer.start = box->u.buffer.start;
        box1->u.buffer.end = box->u.buffer.end;
        box1->content_type = box->content_type;
        box->content_type = CSS_CONTENT_TYPE_CHILDS;
        box->u.child.first = box1;
        box->u.child.last = box1;
        box1->parent = box;
    }
}

CSSContext *css_new_document(QEditScreen *screen,
                             EditBuffer *b)
{
    CSSContext *s;

    s = malloc(sizeof(CSSContext));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(CSSContext));
    s->style_sheet = NULL;
    s->screen = screen;
    s->b = b;
    s->media = screen->media; /* we copy it so that it can be changed
                                 if needed */
    if (s->media == CSS_MEDIA_TTY) {
        s->px_size = (int)(CSS_TTY_PX_SIZE * CSS_LENGTH_FRAC_BASE);
        s->dots_per_inch = CSS_TTY_DPI;
    } else {
        s->px_size = (int)(CSS_SCREEN_PX_SIZE * CSS_LENGTH_FRAC_BASE);
        s->dots_per_inch = CSS_SCREEN_DPI;
    }
    return s;
}

void css_delete_document(CSSContext *s)
{
    int i;
    CSSState *props, *props_next;

    for(i=0;i<PROPS_HASH_SIZE;i++) {
        for(props = s->hash_props[i]; props != NULL; props = props_next) {
            props_next = props->hash_next;
            free_props(props);
        }
    }
    if (s->style_sheet) {
        css_free_style_sheet(s->style_sheet);
    }
    free(s);
}

/* must be called before using any css functions */
void css_init(void)
{
    css_init_idents();
}

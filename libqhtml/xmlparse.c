/*
 * XML/HTML parser for qemacs.
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

/* entities definition */
typedef struct {
    const char *name;
    int val;
} XMLEntity;

#define ENTITY(name, value) { #name, value },

const XMLEntity html_entities[] = {
#include "htmlent.h"
    { NULL, 0 },
};

/* return -1 if not found */
int find_entity(const char *str)
{
    const char *name;
    const XMLEntity *e;
    int code;

    if (str[0] == '#') {
        code = strtol(str + 1, NULL, 10);
        if (code <= 0)
            return -1;
        return code;
    }

    e = html_entities;
    for(;;) {
        name = e->name;
        if (!name)
            break;
        if (!strcmp(str, name))
            return e->val;
        e++;
    }
    return -1;
}

const char *find_entity_str(int code)
{
    const XMLEntity *e;
    e = html_entities;
    for(;;) {
        if (!e->name)
            break;
        if (e->val == code)
            return e->name;
        e++;
    }
    return NULL;
}

/* parse an entity or a normal char (only ASCII chars are expected) */
static int parse_entity(const char **pp)
{
    const char *p, *p1;
    char name[16], *q;
    int ch, ch1;

    p = *pp;
    ch = *p++;
    if (ch == '&') {
        p1 = p;
        q = name;
        for(;;) {
            ch1 = *p;
            if (ch1 == '\0')
                break;
            p++;
            if (ch1 == ';')
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
            p = p1;
        }
    }
    *pp = p;
    return ch;
}

/***********************************************************/
/* XML parser */

enum XMLParseState {
    XML_STATE_TEXT,
    XML_STATE_TAG,
    XML_STATE_COMMENT,
    XML_STATE_COMMENT1,
    XML_STATE_COMMENT2,
    XML_STATE_PRETAG,
    XML_STATE_WAIT_EOT,
};

#define STRING_BUF_SIZE 4096
//#define STRING_BUF_SIZE 256

/* string buffer optimized for strings lengths <= STRING_BUF_SIZE */
typedef struct StringBuffer {
    unsigned char *buf;
    int allocated_size;
    int size;
    unsigned char buf1[STRING_BUF_SIZE];
} StringBuffer;

static inline void strbuf_init(StringBuffer *b)
{
    b->buf = b->buf1;
    b->allocated_size = STRING_BUF_SIZE;
    b->size = 0;
}

static inline void strbuf_reset(StringBuffer *b)
{
    if (b->buf != b->buf1)
        free(b->buf);
    strbuf_init(b);
}

static void strbuf_addch1(StringBuffer *b, int ch)
{
    const char *p;
    int size1;
    unsigned char *ptr;

    size1 = b->allocated_size + STRING_BUF_SIZE;
    ptr = b->buf;
    if (b->buf == b->buf1)
        ptr = NULL;
    ptr = realloc(ptr, size1);
    if (ptr) {
        if (b->buf == b->buf1)
            memcpy(ptr, b->buf1, STRING_BUF_SIZE);
        b->buf = ptr;
        b->allocated_size = size1;

        p = utf8_encode(b->buf + b->size, ch);
        b->size = p - (char *)b->buf;
    }
}

static inline void strbuf_addch(StringBuffer *b, int ch)
{
    const char *p;
    if (b->size < b->allocated_size) {
        /* fast case */
        p = utf8_encode(b->buf + b->size, ch);
        b->size = p - (char *)b->buf;
    } else {
        strbuf_addch1(b, ch);
    }
}

/* offset compression */

typedef struct OffsetBuffer {
    unsigned int *offsets;
    int nb_allocated_offsets;
    int nb_offsets;
    unsigned int last_offset;
} OffsetBuffer;

static void offsetbuf_init(OffsetBuffer *b)
{
    b->offsets = NULL;
    b->nb_allocated_offsets = 0;
    b->nb_offsets = 0;
    b->last_offset = -2; /* ensures that a normal offset will be added
                            first without adding more tests */
}

static void offsetbuf_reset(OffsetBuffer *b)
{
    free(b->offsets);
    offsetbuf_init(b);
}

/* XXX: return an error if memory fails ? */
static void offsetbuf_add(OffsetBuffer *b, unsigned int offset)
{
    int n;

    if (offset == (b->last_offset + 1)) {
        /* just indicate a run of offsets */
        b->offsets[b->nb_offsets - 1] |= 0x80000000;
    } else {
        if (b->nb_offsets >= b->nb_allocated_offsets) {
            n = b->nb_allocated_offsets;
            if (!n)
                n = 1;
            n = n * 2;
            b->offsets = realloc(b->offsets, n * sizeof(unsigned int));
            if (!b->offsets)
                return;
            b->nb_allocated_offsets = n;
        }
        b->offsets[b->nb_offsets++] = offset;
    }
    b->last_offset = offset;
}

#define LOOKAHEAD_SIZE  16

struct XMLState {
    CSSBox *root_box;
    CSSBox *box;
    int is_html;
    int html_syntax;
    int ignore_case;
    int flags;
    CSSStyleSheet *style_sheet; /* if non NULL, all style sheets are
                                   add to that */
    enum XMLParseState state;
    int line_num;
    CSSAbortFunc *abort_func; 
    void *abort_opaque;
    QECharset *charset;
    int base_font; /* XXX: is it correct ? */
    int lookahead_size;
    char lookahead_buf[2 * LOOKAHEAD_SIZE];
    int pretaglen;
    char pretag[32]; /* current tag in XML_STATE_PRETAG */
    StringBuffer str;
    char filename[1024];
    CharsetDecodeState charset_state;
};

/* start xml parsing */

XMLState *xml_begin(CSSStyleSheet *style_sheet, int flags,
                    CSSAbortFunc *abort_func, void *abort_opaque, 
                    const char *filename, QECharset *charset)
{
    XMLState *s;
    
    s = malloc(sizeof(XMLState));
    if (!s)
        return NULL;
    memset(s, 0, sizeof(*s));
    s->flags = flags;
    s->is_html = flags & XML_HTML;
    s->html_syntax = flags & XML_HTML_SYNTAX;
    s->ignore_case = flags & XML_IGNORE_CASE;
    s->state = XML_STATE_TEXT;
    s->box = NULL;
    s->style_sheet = style_sheet;
    strbuf_init(&s->str);
    s->abort_func = abort_func;
    s->abort_opaque = abort_opaque;
    s->base_font = 3;
    s->line_num = 1;
    pstrcpy(s->filename, sizeof(s->filename), filename);
    s->charset = charset;
    if (charset) {
        charset_decode_init(&s->charset_state, charset);
    }
    return s;
}

static CSSAttribute *box_new_attr(CSSIdent attr_id, const char *value)
{
    CSSAttribute *attr;
    attr = malloc(sizeof(CSSAttribute) + strlen(value));
    if (!attr)
        return NULL;
    attr->attr = attr_id;
    attr->next = NULL;
    strcpy(attr->value, value);
    return attr;
}

static const char *css_attr_str(CSSBox *box, CSSIdent attr_id)
{
    CSSAttribute *attr;

    attr = box->attrs;
    while (attr != NULL) {
        if (attr->attr == attr_id)
            return attr->value;
        attr = attr->next;
    }
    return NULL;
}

static const char *css_attr_strlower(CSSBox *box, CSSIdent attr_id)
{
    static char buf[200];
    const char *value;
    value = css_attr_str(box, attr_id);
    if (!value)
        return NULL;
    pstrcpy(buf, sizeof(buf), value);
    css_strtolower(buf, sizeof(buf));
    return buf;
}
    

static int css_attr_int(CSSBox *box, CSSIdent attr_id, int def_val)
{
    const char *str, *p;
    int val;
    str = css_attr_str(box, attr_id);
    if (!str)
        return def_val;
    val = strtol(str, (char **)&p, 10);
    /* exclude non numeric inputs (for example percentages) */
    if (*p != '\0')
        return def_val;
    return val;
}


/* simplistic HTML table border handling */
void html_table_borders(CSSBox *box, int border, int padding)
{
    CSSProperty **last_prop;
    CSSBox *box1;

    if (box->tag == CSS_ID_td) {
        last_prop = &box->properties;
        while (*last_prop != NULL)
            last_prop = &(*last_prop)->next;
        if (border >= 1) {
            css_add_prop_unit(&last_prop, CSS_border_left_width, 
                              CSS_UNIT_PIXEL, border);
            css_add_prop_unit(&last_prop, CSS_border_right_width, 
                              CSS_UNIT_PIXEL, border);
            css_add_prop_unit(&last_prop, CSS_border_top_width,
                              CSS_UNIT_PIXEL, border);
            css_add_prop_unit(&last_prop, CSS_border_bottom_width, 
                              CSS_UNIT_PIXEL, border);

            css_add_prop_int(&last_prop, CSS_border_left_style, 
                             CSS_BORDER_STYLE_RIDGE);
            css_add_prop_int(&last_prop, CSS_border_right_style, 
                             CSS_BORDER_STYLE_RIDGE);
            css_add_prop_int(&last_prop, CSS_border_top_style, 
                             CSS_BORDER_STYLE_RIDGE);
            css_add_prop_int(&last_prop, CSS_border_bottom_style, 
                             CSS_BORDER_STYLE_RIDGE);
        }
        if (padding >= 1) {
            css_add_prop_unit(&last_prop, CSS_padding_left, 
                              CSS_UNIT_PIXEL, padding);
            css_add_prop_unit(&last_prop, CSS_padding_right,
                              CSS_UNIT_PIXEL, padding);
            css_add_prop_unit(&last_prop, CSS_padding_top,
                              CSS_UNIT_PIXEL, padding);
            css_add_prop_unit(&last_prop, CSS_padding_bottom,
                              CSS_UNIT_PIXEL, padding);
        }
    }

    if (box->content_type == CSS_CONTENT_TYPE_CHILDS) {
        for(box1 = box->u.child.first; box1 != NULL; box1 = box1->next) {
            html_table_borders(box1, border, padding);
        }
    }
}

#define DEFAULT_IMG_WIDTH  32
#define DEFAULT_IMG_HEIGHT 32

static void html_eval_tag(XMLState *s, CSSBox *box)
{
    const char *value;
    CSSProperty *first_prop, **last_prop;
    int width, height, color, val, type;
    int border, padding;
    CSSPropertyValue arg;
    CSSPropertyValue args[2];

    first_prop = NULL;
    last_prop = &first_prop;
    switch(box->tag) {
    case CSS_ID_img:
    parse_img:
        box->content_type = CSS_CONTENT_TYPE_IMAGE;
        box->u.image.content_alt = NULL;
        /* set alt content */
        value = css_attr_str(box, CSS_ID_alt);
        if (!value) {
            /* if no alt, display the name of the image */
            value = css_attr_str(box, CSS_ID_src);
            if (value)
                value = basename(value);
        }
        if (value && value[0] != '\0') {
            arg.type = CSS_VALUE_STRING;
            arg.u.str = strdup(value);
            css_add_prop(&last_prop, CSS_content_alt, &arg);
        }
        
        width = css_attr_int(box, CSS_ID_width, 0);
        if (width <= 0)
            width = DEFAULT_IMG_WIDTH;
        height = css_attr_int(box, CSS_ID_height, 0);
        if (height <= 0)
            height = DEFAULT_IMG_HEIGHT;

        css_add_prop_unit(&last_prop, CSS_width, 
                          CSS_UNIT_PIXEL, width);
        css_add_prop_unit(&last_prop, CSS_height, 
                          CSS_UNIT_PIXEL, height);

        /* border */
        val = css_attr_int(box, CSS_ID_border, -1);
        if (val >= 0) {
            css_add_prop_unit(&last_prop, CSS_border_left_width,
                              CSS_UNIT_PIXEL, val);
            css_add_prop_unit(&last_prop, CSS_border_right_width,
                              CSS_UNIT_PIXEL, val);
            css_add_prop_unit(&last_prop, CSS_border_top_width,
                              CSS_UNIT_PIXEL, val);
            css_add_prop_unit(&last_prop, CSS_border_bottom_width,
                              CSS_UNIT_PIXEL, val);

            css_add_prop_int(&last_prop, CSS_border_left_style,
                             CSS_BORDER_STYLE_SOLID);
            css_add_prop_int(&last_prop, CSS_border_right_style,
                             CSS_BORDER_STYLE_SOLID);
            css_add_prop_int(&last_prop, CSS_border_top_style,
                             CSS_BORDER_STYLE_SOLID);
            css_add_prop_int(&last_prop, CSS_border_bottom_style,
                             CSS_BORDER_STYLE_SOLID);
        }
        /* margins */
        val = css_attr_int(box, CSS_ID_hspace, -1);
        if (val >= 0) {
            css_add_prop_unit(&last_prop, CSS_margin_left,
                              CSS_UNIT_PIXEL, val);
            css_add_prop_unit(&last_prop, CSS_margin_right,
                              CSS_UNIT_PIXEL, val);
        }
        val = css_attr_int(box, CSS_ID_vspace, -1);
        if (val >= 0) {
            css_add_prop_unit(&last_prop, CSS_margin_top, 
                              CSS_UNIT_PIXEL, val);
            css_add_prop_unit(&last_prop, CSS_margin_bottom,
                              CSS_UNIT_PIXEL, val);
        }
        break;
    case CSS_ID_body:
        value = css_attr_str(box, CSS_ID_text);
        if (value && !css_get_color(&color, value)) {
            css_add_prop_int(&last_prop, CSS_color, color);
        }
        /* we handle link by adding a new stylesheet entry */
        value = css_attr_str(box, CSS_ID_link);
        if (value && !css_get_color(&color, value)) {
            CSSStyleSheetEntry *e;
            CSSSimpleSelector ss1, *ss = &ss1;
            CSSProperty **last_prop;
            CSSStyleSheetAttributeEntry **plast_attr;

            /* specific to <a href="xxx"> tag */
            memset(ss, 0, sizeof(CSSSimpleSelector));
            ss->tag = CSS_ID_a;
            plast_attr = &ss->attrs;
            add_attribute(&plast_attr, CSS_ID_href, CSS_ATTR_OP_SET, "");
                          
            e = add_style_entry(s->style_sheet, ss, CSS_MEDIA_ALL);

            /* add color property */
            last_prop = &e->props;
            css_add_prop_int(&last_prop, CSS_color, color);
        }
        break;
    case CSS_ID_font:
    case CSS_ID_basefont:
        /* size */
        value = css_attr_str(box, CSS_ID_size);
        if (value) {
            val = strtol(value, NULL, 10);
            if (value[0] == '+' || value[0] == '-') {
                /* relative size */
                val += s->base_font;
            }
            if (val < 1)
                val = 1;
            else if (val > 7)
                val = 7;
            if (box->tag == CSS_ID_basefont)
                s->base_font = val;
            /* XXX: incorrect for basefont */
            css_add_prop_unit(&last_prop, CSS_font_size, 
                              CSS_UNIT_IN, get_font_size(val - 1));
        }
            
        /* color */
        value = css_attr_str(box, CSS_ID_color);
        if (value && !css_get_color(&color, value)) {
            css_add_prop_int(&last_prop, CSS_color, color);
        }
        break;
    case CSS_ID_br:
        value = css_attr_strlower(box, CSS_ID_clear);
        if (value) {
            val = css_get_enum(value, "none,left,right,all");
            if (val >= 0) {
                css_add_prop_int(&last_prop, CSS_clear, val + CSS_CLEAR_NONE);
            }
        }
        break;
    case CSS_ID_table:
        val = css_attr_int(box, CSS_ID_width, -1);
        if (val >= 0) {
            css_add_prop_unit(&last_prop, CSS_width,
                              CSS_UNIT_PIXEL, val);
        }
        border = css_attr_int(box, CSS_ID_border, -1);
        if (border >= 0) {
            css_add_prop_unit(&last_prop, CSS_border_left_width, 
                              CSS_UNIT_PIXEL, border);
            css_add_prop_unit(&last_prop, CSS_border_right_width,
                              CSS_UNIT_PIXEL, border);
            css_add_prop_unit(&last_prop, CSS_border_top_width,
                              CSS_UNIT_PIXEL, border);
            css_add_prop_unit(&last_prop, CSS_border_bottom_width,
                              CSS_UNIT_PIXEL, border);
            css_add_prop_int(&last_prop, CSS_border_left_style,
                             CSS_BORDER_STYLE_GROOVE);
            css_add_prop_int(&last_prop, CSS_border_right_style,
                             CSS_BORDER_STYLE_GROOVE);
            css_add_prop_int(&last_prop, CSS_border_top_style,
                             CSS_BORDER_STYLE_GROOVE);
            css_add_prop_int(&last_prop, CSS_border_bottom_style,
                             CSS_BORDER_STYLE_GROOVE);
            /* cell have a border of 1 pixel width */
            if (border > 1)
                border = 1;
        }
        val = css_attr_int(box, CSS_ID_cellspacing, -1);
        if (val >= 0) {
            css_add_prop_unit(&last_prop, CSS_border_spacing_horizontal, 
                              CSS_UNIT_PIXEL, val);
            css_add_prop_unit(&last_prop, CSS_border_spacing_vertical,
                              CSS_UNIT_PIXEL, val);
        }
        padding = css_attr_int(box, CSS_ID_cellpadding, -1);
        /* apply border styles to each cell (cannot be done exactly by
           CSS) */
        if (border >= 1 || padding >= 1)
            html_table_borders(box, border, padding);
        break;

    case CSS_ID_col:
    case CSS_ID_colgroup:
        val = css_attr_int(box, CSS_ID_width, -1);
        if (val >= 0) {
            css_add_prop_unit(&last_prop, CSS_width, CSS_UNIT_PIXEL, val);
        }
        break;
    case CSS_ID_td:
        val = css_attr_int(box, CSS_ID_width, -1);
        if (val >= 0) {
            css_add_prop_unit(&last_prop, CSS_width, CSS_UNIT_PIXEL, val);
        }
        val = css_attr_int(box, CSS_ID_height, -1);
        if (val >= 0) {
            css_add_prop_unit(&last_prop, CSS_height, CSS_UNIT_PIXEL, val);
        }
        break;

    case CSS_ID_ol:
    case CSS_ID_li:
        /* NOTE: case is important */
        /* XXX: currently cannot propagate for LI tag */
        value = css_attr_str(box, CSS_ID_type);
        if (value) {
            val = css_get_enum(value, "1,a,A,i,I");
            if (val >= 0) {
                val += CSS_LIST_STYLE_TYPE_DECIMAL;
                css_add_prop_int(&last_prop, CSS_list_style_type, val);
            }
        }
        /* XXX: add value, but needs a css extension */
        if (box->tag == CSS_ID_ol) 
            val = CSS_ID_start;
        else
            val = CSS_ID_value;
        /* NOTE: only works with digits */
        val = css_attr_int(box, val, 0);
        if (val > 0) {
            args[0].type = CSS_VALUE_IDENT;
            args[0].u.val = CSS_ID_list_item;
            args[1].type = CSS_VALUE_INTEGER;
            args[1].u.val = val - 1; /* currently needs minus 1 */
            css_add_prop_values(&last_prop, CSS_counter_reset, 2, args);
        }
        break;
        /* controls */
    case CSS_ID_button:
        type = CSS_ID_submit;
        value = css_attr_strlower(box, CSS_ID_type);
        if (value)
            type = css_new_ident(value);
        if (type != CSS_ID_button && type != CSS_ID_reset)
            type = CSS_ID_submit;
        goto parse_input;
    case CSS_ID_input:
        type = CSS_ID_text;
        value = css_attr_strlower(box, CSS_ID_type);
        if (value) {
            type = css_new_ident(value);
        } else {
            CSSAttribute *attr;
            /* NOTE: we add an attribute for css rules */
            attr = box_new_attr(CSS_ID_type, "text");
            if (attr) {
                attr->next = box->attrs;
                box->attrs = attr;
            }
        }
            
    parse_input:
        if (type == CSS_ID_image)
            goto parse_img;
        if (type == CSS_ID_button ||
            type == CSS_ID_reset ||
            type == CSS_ID_submit ||
            type == CSS_ID_text ||
            type == CSS_ID_password ||
            type == CSS_ID_file) {
            /* put text inside the box (XXX: use attr() in content
               attribute ? */
            value = css_attr_str(box, CSS_ID_value);
            if (value) {
                css_set_text_string(box, value);
            }
        }
        /* size */
        if (type == CSS_ID_text ||
            type == CSS_ID_password) {
            val = css_attr_int(box, CSS_ID_size, 10);
            css_add_prop_unit(&last_prop, CSS_width,
                              CSS_UNIT_EM, val << CSS_LENGTH_FRAC_BITS);
        }
        break;
    case CSS_ID_textarea:
        val = css_attr_int(box, CSS_ID_cols, 10);
        if (val < 1)
            val = 1;
        css_add_prop_unit(&last_prop, CSS_width, 
                          CSS_UNIT_EM, val << CSS_LENGTH_FRAC_BITS);

        val = css_attr_int(box, CSS_ID_rows, 1);
        if (val < 1)
            val = 1;
        css_add_prop_unit(&last_prop, CSS_height, 
                          CSS_UNIT_EM, val << CSS_LENGTH_FRAC_BITS);
        break;
    case CSS_ID_select:
        val = css_attr_int(box, CSS_ID_size, 1);
        if (val < 1)
            val = 1;
        css_add_prop_unit(&last_prop, CSS_height, 
                          CSS_UNIT_EM, val << CSS_LENGTH_FRAC_BITS);
        break;
    default:
        break;
    }

    /* generic attributes */
    value = css_attr_str(box, CSS_ID_bgcolor);
    if (value && !css_get_color(&color, value)) {
        css_add_prop_int(&last_prop, CSS_background_color, color);
    }
    value = css_attr_strlower(box, CSS_ID_align);
    if (value) {
        switch(box->tag) {
        case CSS_ID_caption:
            /* use caption-side property for captions */
            val = css_get_enum(value, "top,bottom,left,right");
            if (val >= 0) {
                css_add_prop_int(&last_prop, CSS_caption_side, val);
            }
            break;
        case CSS_ID_img:
            /* floating images */
            val = css_get_enum(value, "left,right");
            if (val >= 0) {
                css_add_prop_int(&last_prop, CSS_float, val + CSS_FLOAT_LEFT);
            }
            break;
        case CSS_ID_table:
            val = css_get_enum(value, "left,right,center");
            if (val == CSS_TEXT_ALIGN_LEFT || val == CSS_TEXT_ALIGN_RIGHT) {
                css_add_prop_int(&last_prop, CSS_float, val + CSS_FLOAT_LEFT);
            } else if (val == CSS_TEXT_ALIGN_CENTER) {
                css_add_prop_int(&last_prop, CSS_margin_left, CSS_AUTO);
                css_add_prop_int(&last_prop, CSS_margin_right, CSS_AUTO);
            }
            break;
        default:
            val = css_get_enum(value, "left,right,center");
            if (val >= 0) {
                css_add_prop_int(&last_prop, CSS_text_align, val);
            }
            break;
        }
    }
    value = css_attr_strlower(box, CSS_ID_valign);
    if (value) {
        val = css_get_enum(value, "baseline,,,top,,middle,bottom");
        if (val >= 0) {
            css_add_prop_int(&last_prop, CSS_vertical_align, val);
        }
    }

    val = css_attr_int(box, CSS_ID_colspan, 1);
    if (val > 1) {
        css_add_prop_unit(&last_prop, CSS_column_span, CSS_VALUE_INTEGER, val);
    }
    val = css_attr_int(box, CSS_ID_rowspan, 1);
    if (val > 1) {
        css_add_prop_unit(&last_prop, CSS_row_span, CSS_VALUE_INTEGER, val);
    }

    value = css_attr_str(box, CSS_ID_style);
    if (value) {
        CSSParseState b1, *b = &b1;
        b->ptr = NULL;
        b->line_num = s->line_num; /* XXX: slightly incorrect */
        b->filename = s->filename;
        b->ignore_case = s->ignore_case;
        *last_prop = css_parse_properties(b, value);
    }
    box->properties = first_prop;
}

static void xml_error(XMLState *s, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    css_error(s->filename, s->line_num, buf);
    va_end(ap);
}

/* XXX: avoid using strings in tag_closed and generalize */
typedef struct {
    CSSIdent tag;    /* tag to handle */
    const char *tag_closed;  /* all the following tags can be closed
                                automatically */
} HTMLClosedTags;

static const HTMLClosedTags html_closed_tags[] = {
    { CSS_ID_li, "li,b,i,em,s,u,strike,strong,a" },
    { CSS_ID_td, "td,th,b,i,em,s,u,strike,strong,a,li" },
    { CSS_ID_th, "td,th,b,i,em,s,u,strike,strong,a,li" },
    { CSS_ID_tr, "tr,td,th,b,i,em,s,u,strike,strong,a,li" },
    { CSS_ID_dt, "dd,b,i,em,s,u,strike,strong,a" },
    { CSS_ID_dd, "dt,b,i,em,s,u,strike,strong,a" },
    { CSS_ID_b, "i,em,s,u,strike,strong" },
    { CSS_ID_table, "font" },
    { CSS_ID_NIL, NULL },
};

static int parse_tag(XMLState *s, const char *buf)
{
    char tag[256], *q, len, eot;
    char attr_name[256];
    char value[2048];
    const char *p;
    CSSIdent css_tag;
    CSSBox *box, *box1;
    CSSAttribute *first_attr, **pattr, *attr;

    p = buf;
    
    /* ignore XML commands */
    if (p[0] == '!' || p[0] == '?')
        return XML_STATE_TEXT;

    /* end of tag check */
    eot = 0;
    if (*p == '/') {
        p++;
        eot = 1;
    }

    /* parse the tag name */
    get_str(&p, tag, sizeof(tag), " \t\n\r/");
    if (tag[0] == '\0') {
        /* special closing tag */
        if (eot) {
            css_tag = CSS_ID_NIL;
            goto end_of_tag;
        } else {
            xml_error(s, "invalid null tag");
            return XML_STATE_TEXT;
        }
    }
    if (s->ignore_case)
        css_strtolower(tag, sizeof(tag));
    css_tag = css_new_ident(tag);
    
    /* XXX: should test html_syntax, but need more patches */
    if (s->is_html && (css_tag == CSS_ID_style || 
                       css_tag == CSS_ID_script)) 
        goto pretag;
    if (eot)
        goto end_of_tag;

    /* parse attributes */
    first_attr = NULL;
    pattr = &first_attr;
    for(;;) {
        skip_spaces(&p);
        if (*p == '\0' || *p == '/')
            break;
        get_str(&p, attr_name, sizeof(attr_name), " \t\n\r=/");
        if (s->ignore_case)
            css_strtolower(attr_name, sizeof(attr_name));
        skip_spaces(&p);
        if (*p == '=') {
            int och, ch;
            p++;
            skip_spaces(&p);
            och = *p;
            /* in html, we can put non string values */
            if (och != '\'' && och != '\"') {
                if (!s->html_syntax)
                    xml_error(s, "string expected for attribute '%s'", attr_name);
                q = value;
                while (*p != '\0' && !strchr(" \t\n\r<>", *p)) {
                    ch = parse_entity(&p);
                    if ((q - value) < sizeof(value) - 1) 
                        *q++ = ch;
                }
                *q = '\0';
            } else {
                p++;
                q = value;
                while (*p != och && *p != '\0' && *p != '<') {
                    ch = parse_entity(&p);
                    if ((q - value) < sizeof(value) - 1) 
                        *q++ = ch;
                }
                *q = '\0';
                if (*p != och) {
                    xml_error(s, "malformed string in attribute '%s'", attr_name);
                } else {
                    p++;
                }
            }
        } else {
            value[0] = '\0';
        }
        attr = box_new_attr(css_new_ident(attr_name), value);
        if (attr) {
            *pattr = attr;
            pattr = &attr->next;
        }
    }

    /* close some tags (correct HTML mistakes) */
    if (s->html_syntax) {
        CSSBox *box1;
        const HTMLClosedTags *ct;
        ct = html_closed_tags;
        for(;;) {
            if (!ct->tag)
                break;
            if (css_tag == ct->tag) {
                box1 = s->box;
                while (box1 != NULL &&
                       css_get_enum(css_ident_str(box1->tag), ct->tag_closed) >= 0) {
                    html_eval_tag(s, box1);
                    box1 = box1->parent;
                }
                if (box1) {
                    s->box = box1;
                }
                break;
            }
            ct++;
        }
    }
    
    /* create the new box and add it */
    box = css_new_box(css_tag, NULL);
    box->attrs = first_attr;
    if (!s->box) {
        s->root_box = box;
    } else {
        css_make_child_box(s->box);
        css_add_box(s->box, box);
    }
    s->box = box;
    
    if ((s->flags & XML_DOCBOOK) && 
        css_tag == CSS_ID_programlisting) {
    pretag:
        pstrcpy(s->pretag, sizeof(s->pretag), tag);
        s->pretaglen = strlen(s->pretag);
        return XML_STATE_PRETAG;
    }

    len = strlen(buf);
    /* end of tag. If html, check also some common mistakes. FORM is
       considered as self closing to avoid any content problems */
    if ((len > 0 && buf[len - 1] == '/') ||
        (s->html_syntax && (css_tag == CSS_ID_br ||
                            css_tag == CSS_ID_hr ||
                            css_tag == CSS_ID_meta ||
                            css_tag == CSS_ID_link ||
                            css_tag == CSS_ID_form ||
                            css_tag == CSS_ID_base ||
                            css_tag == CSS_ID_input ||
                            css_tag == CSS_ID_basefont ||
                            css_tag == CSS_ID_img))) {
    end_of_tag:
        box1 = s->box;
        if (box1) {
            if (s->html_syntax) {
                if (css_tag != CSS_ID_NIL) {
                    /* close all non matching tags */
                    while (box1 != NULL && box1->tag != css_tag) {
                        html_eval_tag(s, box1);
                        box1 = box1->parent;
                    }
                }
                if (!box1) {
                    if (css_tag != CSS_ID_form)
                        xml_error(s, "unmatched closing tag </%s>", 
                                  css_ident_str(css_tag));
                } else {
                    html_eval_tag(s, box1);
                    s->box = box1->parent;
                }
            } else {
                if (css_tag != CSS_ID_NIL && box1->tag != css_tag) {
                    xml_error(s, "unmatched closing tag </%s> for <%s>",
                              css_ident_str(css_tag), css_ident_str(box1->tag));
                } else {
                    if (s->is_html)
                        html_eval_tag(s, box1);
                    s->box = box1->parent;
                }
            }
        }
    }
    return XML_STATE_TEXT;
}

static void flush_text(XMLState *s, const char *buf)
{
    CSSBox *box, *box1;

    box = s->box;
    /* if no current box, discard */
    if (!box)
        return;

    if (buf[0] == '\0')
        return;

    css_make_child_box(box);

    if (box->u.child.first != NULL) {
        /* non empty box: we add an anonymous box */
        box1 = css_new_box(CSS_ID_NIL, NULL);
        css_add_box(box, box1);
    } else {
        box1 = box;
    }
    css_set_text_string(box1, buf);
}

static void flush_text_buffer(XMLState *s, int offset0, int offset1)
{
    CSSBox *box, *box1;
    
    box = s->box;
    /* if no current box, discard */
    if (!box)
        return;

    if (offset0 >= offset1)
        return;

    css_make_child_box(box);

    if (box->u.child.first != NULL) {
        /* non empty box: we add an anonymous box */
        box1 = css_new_box(CSS_ID_NIL, NULL);
        css_add_box(box, box1);
    } else {
        box1 = box;
    }
    css_set_text_buffer(box1, offset0, offset1, 1);
}

static int xml_tagcmp(const char *s1, const char *s2)
{
    int d;
    while (*s2) {
        d = *s2 - tolower(*s1);
        if (d)
            return d;
        s2++;
        s1++;
    }
    return 0;
}


static int xml_parse_internal(XMLState *s, const char *buf_start, int buf_len,
                              EditBuffer *b, int offset_start)
{
    int ch, offset, offset0, text_offset_start, ret, offset_end;
    const char *buf_end, *buf;

    buf = buf_start;
    buf_end = buf + buf_len;
    offset = offset_start;
    offset_end = offset_start + buf_len;
    offset0 = 0; /* not used */
    text_offset_start = 0; /* not used */
    for(;;) {
        if (buf) {
            if (buf >= buf_end)
                break;
            ch = charset_decode(&s->charset_state, &buf);
        } else {
            if (offset >= offset_end)
                break;
            offset0 = offset;
            ch = eb_nextc(b, offset, &offset);
        }
        /* increment line number to signal errors */
        if (ch == '\n') {
            /* well, should add counter, but we test abort here */
            if (s->abort_func(s->abort_opaque))
                return -1;
            s->line_num++;
        }

        switch(s->state) {
        case XML_STATE_TAG:
            if (ch == '>') {
                strbuf_addch(&s->str, '\0');
                ret = parse_tag(s, s->str.buf);
                switch(ret) {
                default:
                case XML_STATE_TEXT:
                xml_text:
                    strbuf_reset(&s->str);
                    s->state = XML_STATE_TEXT;
                    text_offset_start = offset;
                    break;
                case XML_STATE_PRETAG:
                    strbuf_reset(&s->str);
                    s->state = XML_STATE_PRETAG;
                    text_offset_start = offset;
                    break;
                }
            } else {
                strbuf_addch(&s->str, ch);
                /* test comment */
                if (s->str.size == 3 &&
                    s->str.buf[0] == '!' &&
                    s->str.buf[1] == '-' &&
                    s->str.buf[2] == '-') {
                    s->state = XML_STATE_COMMENT;
                }
            }
            break;
        case XML_STATE_TEXT:
            if (ch == '<') {
                /* XXX: not strictly correct with comments : should
                   not flush if comment */
                if (buf) {
                    strbuf_addch(&s->str, '\0');
                    flush_text(s, s->str.buf);
                    strbuf_reset(&s->str);
                } else {
                    flush_text_buffer(s, text_offset_start, offset0);
                }
                s->state = XML_STATE_TAG;
            } else {
                if (buf) {
                    /* evaluate entities */
                    if (ch == '&') {
                        buf--;
                        ch = parse_entity(&buf);
                    }
                    strbuf_addch(&s->str, ch);
                }
            }
            break;
        case XML_STATE_COMMENT:
            if (ch == '-')
                s->state = XML_STATE_COMMENT1;
            break;
        case XML_STATE_COMMENT1:
            if (ch == '-')
                s->state = XML_STATE_COMMENT2;
            else
                s->state = XML_STATE_COMMENT;
            break;
        case XML_STATE_COMMENT2:
            if (ch == '>') {
                goto xml_text;
            } else if (ch != '-') {
                s->state = XML_STATE_COMMENT;
            }
            break;
        case XML_STATE_PRETAG:
            {
                int len, taglen;

                strbuf_addch(&s->str, ch);
                taglen = s->pretaglen + 2;
                len = s->str.size - taglen;
                if (len >= 0 && 
                    s->str.buf[len] == '<' && 
                    s->str.buf[len + 1] == '/' &&
                    !xml_tagcmp(s->str.buf + len + 2, s->pretag)) {
                    s->str.buf[len] = '\0';
                    
                    if (!xml_tagcmp(s->pretag, "style")) {
                        if (s->style_sheet) {
                            CSSParseState b1, *b = &b1;
                            b->ptr = s->str.buf;
                            b->line_num = s->line_num; /* XXX: incorrect */
                            b->filename = s->filename;
                            b->ignore_case = s->ignore_case;
                            css_parse_style_sheet(s->style_sheet, b);
                        }
                    } else if (!xml_tagcmp(s->pretag, "script")) {
                        /* XXX: handle script */
                    } else {
                        /* just add the content */
                        if (buf) {
                            flush_text(s, s->str.buf);
                        } else {
                            /* XXX: would be incorrect if non ascii chars */
                            flush_text_buffer(s, text_offset_start, offset - taglen);
                        }
                        strbuf_reset(&s->str);
                        if (s->box)
                            s->box = s->box->parent;
                    }
                    s->state = XML_STATE_WAIT_EOT;
                }
            }
            break;
        case XML_STATE_WAIT_EOT:
            /* wait end of tag */
            if (ch == '>')
                    goto xml_text;
            break;
        }
    }
    return buf - buf_start;
}

int xml_parse(XMLState *s, char *buf, int buf_len)
{
    int len, len1;

    if (s->lookahead_size > 0) {
        /* fill the lookahead buffer with the content of 'buf' and
           handle all chars which are in lookahead_buf */
        len = LOOKAHEAD_SIZE - 1;
        if (len > buf_len)
            len = buf_len;
        memcpy(s->lookahead_buf + s->lookahead_size, buf, len);
        len += s->lookahead_size; /* total chars in lookahead buffer */

        /* parse only if enough chars for lookahead */
        len -= (LOOKAHEAD_SIZE - 1);
        if (len > 0) {
            len = xml_parse_internal(s, s->lookahead_buf, len, NULL, 0);
            if (len < 0)
                return -1;
            len1 = len - s->lookahead_size;
            if (len1 <= 0) {
                s->lookahead_size -= len;
                memmove(s->lookahead_buf, s->lookahead_buf + len, s->lookahead_size);
            } else {
                buf_len -= len1;
                buf += len1;
                s->lookahead_size = 0;
            }
            /* if could not parse everything, no need to go further */
            if (s->lookahead_size > 0)
                return 0;
        } else {
            /* all chars were put in lookahead buffer */
            s->lookahead_size = len + (LOOKAHEAD_SIZE - 1);
            return 0;
        }
    }
        
    /* now no chars are left in the lookahead buffer so we can parse at full speed */
    len1 = buf_len - (LOOKAHEAD_SIZE - 1);
    if (len1 > 0) {
        len = xml_parse_internal(s, buf, len1, NULL, 0);
        if (len < 0)
            return -1;
        buf_len -= len;
        buf += len;
    }
    
    /* put all the remaining chars in the lookahead buffer */
    memcpy(s->lookahead_buf, buf, buf_len);
    s->lookahead_size = buf_len;
    return 0;
}


CSSBox *xml_end(XMLState *s)
{
    CSSBox *root_box;

    /* flush the lookahead buffer */
    if (s->lookahead_size > 0) {
        /* mark the end to stop parsing function */
        s->lookahead_buf[s->lookahead_size] = '\0';
        xml_parse_internal(s, s->lookahead_buf, s->lookahead_size, NULL, 0);
    }

    if (s->charset) {
        charset_decode_close(&s->charset_state);
        s->charset = NULL;
    }

    strbuf_reset(&s->str);
    root_box = s->root_box;
    
    free(s);
    return root_box;
}

/* XML in edit buffer parsing */
CSSBox *xml_parse_buffer(EditBuffer *b, int offset_start, int offset_end, 
                         CSSStyleSheet *style_sheet, int flags,
                         CSSAbortFunc *abort_func, void *abort_opaque)
{
    XMLState *s;
    CSSBox *box;
    int ret;

    s = xml_begin(style_sheet, flags, abort_func, abort_opaque, b->name, NULL);
    ret = xml_parse_internal(s, NULL, offset_end - offset_start, 
                             b, offset_start);
    box = xml_end(s);
    if (ret < 0) {
        css_delete_box(box);
        box = NULL;
    }
    return box;
}


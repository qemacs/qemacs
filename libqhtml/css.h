#ifndef CSS_H
#define CSS_H

struct CSSBox;

typedef struct CSSBox CSSBox;

/* display property */
enum CSSDisplay {
    CSS_DISPLAY_INLINE,
    CSS_DISPLAY_BLOCK,
    CSS_DISPLAY_TABLE,
    CSS_DISPLAY_TABLE_ROW,
    CSS_DISPLAY_TABLE_ROW_GROUP,
    CSS_DISPLAY_TABLE_HEADER_GROUP,
    CSS_DISPLAY_TABLE_FOOTER_GROUP,
    CSS_DISPLAY_TABLE_COLUMN,
    CSS_DISPLAY_TABLE_COLUMN_GROUP,
    CSS_DISPLAY_TABLE_CELL,
    CSS_DISPLAY_TABLE_CAPTION,
    CSS_DISPLAY_LIST_ITEM,
    CSS_DISPLAY_MARKER,
    CSS_DISPLAY_INLINE_BLOCK, /* qemacs extension (CSS3 ?) */
    CSS_DISPLAY_INLINE_TABLE,
    CSS_DISPLAY_NONE,
};

enum CSSProperties {
    CSS_display,
    CSS_color,
    CSS_background_color,
    CSS_white_space,
#define CSS_WHITE_SPACE_NORMAL  0
#define CSS_WHITE_SPACE_PRE     1
#define CSS_WHITE_SPACE_NOWRAP  2
#define CSS_WHITE_SPACE_PREWRAP 3 /* qemacs extension for editing
                                     (XXX: use CSS3)*/
    CSS_direction,
#define CSS_DIRECTION_LTR       0
#define CSS_DIRECTION_RTL       1
    CSS_float,
#define CSS_FLOAT_NONE  0
#define CSS_FLOAT_LEFT  1
#define CSS_FLOAT_RIGHT 2
    CSS_font_family,
    CSS_font_style,
#define CSS_FONT_STYLE_NORMAL 0
#define CSS_FONT_STYLE_ITALIC 1
    CSS_font_weight,
#define CSS_FONT_WEIGHT_NORMAL  0
#define CSS_FONT_WEIGHT_BOLD    1
#define CSS_FONT_WEIGHT_BOLDER  2
#define CSS_FONT_WEIGHT_LIGHTER 3
    CSS_font_size,
    CSS_text_decoration,
#define CSS_TEXT_DECORATION_NONE         0
#define CSS_TEXT_DECORATION_UNDERLINE    1
#define CSS_TEXT_DECORATION_LINE_THROUGH 2
    CSS_text_align,
#define CSS_TEXT_ALIGN_LEFT    0
#define CSS_TEXT_ALIGN_RIGHT   1
#define CSS_TEXT_ALIGN_CENTER  2
    CSS_width,
    CSS_height,
    CSS_unicode_bidi,
#define CSS_UNICODE_BIDI_NORMAL   0
#define CSS_UNICODE_BIDI_EMBED    1
#define CSS_UNICODE_BIDI_OVERRIDE 2
    CSS_border_width,
    CSS_border_left_width,
    CSS_border_top_width,
    CSS_border_right_width,
    CSS_border_bottom_width,

    CSS_border_color,
    CSS_border_left_color,
    CSS_border_top_color,
    CSS_border_right_color,
    CSS_border_bottom_color,

    CSS_border_style,
    CSS_border_left_style,
    CSS_border_top_style,
    CSS_border_right_style,
    CSS_border_bottom_style,
#define CSS_BORDER_STYLE_NONE    0
#define CSS_BORDER_STYLE_HIDDEN  1
#define CSS_BORDER_STYLE_DOTTED  2
#define CSS_BORDER_STYLE_DASHED  3
#define CSS_BORDER_STYLE_SOLID   4
#define CSS_BORDER_STYLE_DOUBLE  5
#define CSS_BORDER_STYLE_GROOVE  6
#define CSS_BORDER_STYLE_RIDGE   7
#define CSS_BORDER_STYLE_INSET   8
#define CSS_BORDER_STYLE_OUTSET  9

    CSS_border,
    CSS_border_left,
    CSS_border_top,
    CSS_border_right,
    CSS_border_bottom,
    
    CSS_padding,
    CSS_padding_left,
    CSS_padding_top,
    CSS_padding_right,
    CSS_padding_bottom,

    CSS_margin,
    CSS_margin_left,
    CSS_margin_top,
    CSS_margin_right,
    CSS_margin_bottom,
    CSS_clear,
#define CSS_CLEAR_NONE  0
#define CSS_CLEAR_LEFT  1
#define CSS_CLEAR_RIGHT 2
#define CSS_CLEAR_BOTH  3
    CSS_overflow,
#define CSS_OVERFLOW_VISIBLE 0
#define CSS_OVERFLOW_HIDDEN  1
    CSS_visibility,
#define CSS_VISIBILITY_VISIBLE 0
#define CSS_VISIBILITY_HIDDEN  1
    CSS_table_layout,
#define CSS_TABLE_LAYOUT_AUTO  0
#define CSS_TABLE_LAYOUT_FIXED 1
    CSS_vertical_align,
#define CSS_VERTICAL_ALIGN_BASELINE     0
#define CSS_VERTICAL_ALIGN_SUB          1
#define CSS_VERTICAL_ALIGN_SUPER        2
#define CSS_VERTICAL_ALIGN_TOP          3
#define CSS_VERTICAL_ALIGN_TEXT_TOP     4
#define CSS_VERTICAL_ALIGN_MIDDLE       5
#define CSS_VERTICAL_ALIGN_BOTTOM       6
#define CSS_VERTICAL_ALIGN_TEXT_BOTTOM  7
    CSS_border_collapse,
#define CSS_BORDER_COLLAPSE_COLLAPSE    0
#define CSS_BORDER_COLLAPSE_SEPARATE    1
    CSS_border_spacing,
    CSS_border_spacing_horizontal,
    CSS_border_spacing_vertical,
    CSS_line_height,
    CSS_position,
#define CSS_POSITION_STATIC     0
#define CSS_POSITION_RELATIVE   1
#define CSS_POSITION_ABSOLUTE   2
#define CSS_POSITION_FIXED      3
    CSS_content,
    CSS_caption_side,
#define CSS_CAPTION_SIDE_TOP    0
#define CSS_CAPTION_SIDE_BOTTOM 1
#define CSS_CAPTION_SIDE_LEFT   2
#define CSS_CAPTION_SIDE_RIGHT  3
    CSS_marker_offset,
    CSS_list_style_type,
#define CSS_LIST_STYLE_TYPE_DISC        0
#define CSS_LIST_STYLE_TYPE_CIRCLE      1
#define CSS_LIST_STYLE_TYPE_SQUARE      2
#define CSS_LIST_STYLE_TYPE_DECIMAL     3
#define CSS_LIST_STYLE_TYPE_LOWER_ALPHA 4
#define CSS_LIST_STYLE_TYPE_UPPER_ALPHA 5
#define CSS_LIST_STYLE_TYPE_LOWER_ROMAN 6
#define CSS_LIST_STYLE_TYPE_UPPER_ROMAN 7
#define CSS_LIST_STYLE_TYPE_NONE        8
    CSS_column_span, /* qemacs extension */
    CSS_row_span,    /* qemacs extension */
    CSS_content_alt, /* qemacs extension */
    CSS_list_style_position,
#define CSS_LIST_STYLE_POSITION_OUTSIDE 0
#define CSS_LIST_STYLE_POSITION_INSIDE  1
    CSS_counter_reset,
    CSS_counter_increment,
    CSS_bidi_mode, /* qemacs extension */
#define CSS_BIDI_MODE_NORMAL            0
#define CSS_BIDI_MODE_TEST              1
    CSS_left,
    CSS_top,
    CSS_right,
    CSS_bottom,
    NB_PROPERTIES,
};

#define CSS_AUTO    0x80000000
#define CSS_INHERIT 0x80000001

typedef unsigned int CSSColor;

typedef struct CSSPropertyDef {
    const char *name;
    unsigned short struct_offset; /* in CSSState */
    unsigned char storage;
#define CSS_STORAGE_INT     0
#define CSS_STORAGE_PTR     1
    unsigned int type;
#define CSS_TYPE_AUTO          0x80000000
#define CSS_TYPE_NOINHERIT     0x40000000
/* the four next properties will be set according to the CSS rules for
   margin, borders and padding */
#define CSS_TYPE_FOUR          0x20000000
#define CSS_TYPE_TWO           0x10000000 /* the same with two args */
#define CSS_TYPE_SPECIAL       0x08000000 /* XXX: special storage needed */
#define CSS_TYPE_INHERITED     0x04000000 /* property is inherited */
#define CSS_TYPE_ARGS          0x02000000 /* no limit on number of args */
/* types can be ored */
#define CSS_TYPE_LENGTH        0x00000001
#define CSS_TYPE_COLOR         0x00000002
#define CSS_TYPE_ENUM          0x00000004
#define CSS_TYPE_BORDER_STYLE  0x00000008  
#define CSS_TYPE_FONT_FAMILY   0x00000010
#define CSS_TYPE_BORDER_ENUM   0x00000020
#define CSS_TYPE_STRING        0x00000040
#define CSS_TYPE_INTEGER       0x00000080
#define CSS_TYPE_ATTR          0x00000100 /* attr(x) */
#define CSS_TYPE_COUNTER       0x00000200 /* counter(x[,type]) */
#define CSS_TYPE_LIST_STYLE    0x00000400
#define CSS_TYPE_IDENT         0x00000800

} CSSPropertyDef;

extern const CSSPropertyDef css_properties[NB_PROPERTIES];

/* CSS parsing */

/* length are normalized to this value, except pixel units */
#define CSS_LENGTH_FRAC_BITS 8
#define CSS_LENGTH_FRAC_BASE (1 << CSS_LENGTH_FRAC_BITS)

/* number of pixels for one px */
#define CSS_SCREEN_PX_SIZE   1
/* resolution */
#define CSS_SCREEN_DPI       72

#define CSS_TTY_PX_SIZE      (1.0/8.0)
#define CSS_TTY_DPI          9

/* currently, we define ex as a scaling of the font size */
#define CSS_EX_SCALE         ((int)(0.8 * CSS_LENGTH_FRAC_BASE))

#define CSS_UNIT_NONE       0
#define CSS_UNIT_PIXEL      1
#define CSS_UNIT_PERCENT    2
#define CSS_UNIT_EX         3
#define CSS_UNIT_EM         4
#define CSS_UNIT_IN         6

/* the following units are never used except in parsing */
#define CSS_UNIT_MM         5
#define CSS_UNIT_CM         7
#define CSS_UNIT_PT         8
#define CSS_UNIT_PC         9

/* additionnal values */
#define CSS_VALUE_STRING     11
#define CSS_VALUE_COUNTER    12
#define CSS_VALUE_ATTR       13
#define CSS_VALUE_COLOR      14
#define CSS_VALUE_IDENT      15
#define CSS_VALUE_INTEGER    16

/* CSS identifier handling */
typedef enum CSSIdent {
    CSS_ID_NIL = 0,
    CSS_ID_ALL = 1,    /* '*' ident */
#define CSSID(id) CSS_ID_ ## id,
#define CSSIDSTR(id, str) CSS_ID_ ## id,
#include "cssid.h"
#undef CSSID
#undef CSSIDSTR
} CSSIdent;

typedef struct CSSIdentEntry {
    CSSIdent id;
    struct CSSIdentEntry *hash_next;
    char str[1];
} CSSIdentEntry;

const char *css_ident_str(CSSIdent id);
CSSIdent css_new_ident(const char *str);

static inline unsigned int css_hash_ident(CSSIdent id, unsigned int hash_size)
{
    return (unsigned int)id % hash_size;
}

/* property handlign */

typedef struct CSSPropertyValue {
    int type; /* see CSS_UNIT_xxx or CSS_VALUE_xxx */
    union {
        int val;
        char *str;
        CSSIdent attr_id;
        struct {
            CSSIdent counter_id;
            int type;
        } counter;
    } u;
} CSSPropertyValue;

typedef struct CSSProperty {
    unsigned short property;
    unsigned short nb_values;
    struct CSSProperty *next; /* may change: use array ? */
    CSSPropertyValue value;   /* in fact an array, but need 
                                 to change all the types */
} CSSProperty;

/* tag attribute handling */

typedef struct CSSAttribute {
    CSSIdent attr;
    struct CSSAttribute *next;
    char value[1];
} CSSAttribute;

/* css box definition (most important data structure) */

struct CSSBox {
    CSSIdent tag;          /* tag name of the box, may be CSS_ID_NIL
                              for anonymous box */
    CSSAttribute *attrs;   /* attributes (may be NULL) */
    CSSProperty *properties;  /* explicit properties */
    struct CSSState *props;          /* computed properties for this box */
    struct CSSBox *next; /* next box at same level */
    /* bounding box of the draw primitives of the box and all its
       childs */
    CSSRect bbox;
    /* layout info */
    int x, y; /* absolute content position. XXX: currently also
                 relative to parent in the layout */
    int width, height; /* content width & height */
    /* inline specific layout. Not useful in fact XXX: use an union,
       or merge with padding_xx */
    unsigned short padding_top;
    unsigned short padding_bottom;
    unsigned short ascent; /* cannot use font ascent in case of
    fallback XXX: find a way to suppress this field */
    unsigned char embedding_level; /* from layout: bidir embedding level */
    unsigned char content_type;
#define CSS_CONTENT_TYPE_CHILDS  0
#define CSS_CONTENT_TYPE_BUFFER  1
#define CSS_CONTENT_TYPE_STRING  2
#define CSS_CONTENT_TYPE_IMAGE   3
    /* true if the content ends with eol. (used in
       CSS_CONTENT_TYPE_BUFFER to display the cursor */
    unsigned char content_eol:1;
    unsigned char absolute_pos:1; /* true if absolute position (only
                                     meaningful during layout */
    unsigned char split:1;        /* true if this box is a splitted box
                                     (no need to free its content) */
    /* true if there was a space in the previous box (useful in inline
       formatting context) */
    unsigned char last_space;
    /* next inline box in an inline formatting context. Only used
       during bidi pass, so we could put this field in another field
       to save space */
    struct CSSBox *next_inline; 
    /* parent box */
    struct CSSBox *parent; 
    union {
        struct {
            struct CSSBox *last;  /* used only when building the tree */
            struct CSSBox *first; /* first box under this box, if any */
        } child;
        /* either pointers to memory or in a buffer */
        struct {
            unsigned long start;       /* start of text */
            unsigned long end;         /* end of text */
        } buffer;
        struct {
            char *content_alt;         /* alternate content */
        } image;
    } u;
};

/* horrible, but simple: we store EOL content (\A) as this value. Need
   to find a better unicode value and to store it in UTF8 */
#define CSS_CONTENT_EOL     0x01

typedef struct CSSState {
    int display;
    CSSColor color;
    CSSColor bgcolor;
    CSSColor border_colors[4];
    CSSRect padding;
    CSSRect border;
    CSSRect margin;
    int border_styles[4];
    int white_space;
    int direction;
    int block_float;
    int font_family;
    int font_style;
    int text_decoration;
    int font_weight;
    int font_size;
    int text_align;
    int width;
    int height;
    int unicode_bidi;
    int clear;
    int overflow;
    int visibility; /* XXX: initial value should be inherit */
    int table_layout;
    int vertical_align;
    int border_collapse;
    int border_spacing_horizontal;
    int border_spacing_vertical;
    int line_height;
    int position;
    int caption_side;
    int marker_offset;
    int list_style_type;
    int column_span;
    int row_span;
    int list_style_position;
    int bidi_mode;
    int left;
    int top;
    int right;
    int bottom;
    /* after hash_next, no hashing or bulk comparisons are done */
    struct CSSState *hash_next;
    /* set of complex properties which are handled once after the
       cascade is done */
    struct CSSProperty *content;
    struct CSSProperty *content_alt;
    struct CSSProperty *counter_reset;
    struct CSSProperty *counter_increment;
} CSSState;

/* style sheet handling */

typedef struct CSSStyleSheetAttributeEntry {
    struct CSSStyleSheetAttributeEntry *next;
    CSSIdent attr;        /* attribute */
    unsigned char op;
#define CSS_ATTR_OP_SET      0
#define CSS_ATTR_OP_EQUAL    1
#define CSS_ATTR_OP_IN_LIST  2
#define CSS_ATTR_OP_IN_HLIST 3
    char value[1];
} CSSStyleSheetAttributeEntry;

/* simple selector */
typedef struct CSSSimpleSelector {
    /* relation between next and current simple selector */
    unsigned char tree_op;
#define CSS_TREE_OP_NONE       0  /* no next simple selector */
#define CSS_TREE_OP_DESCENDANT 1
#define CSS_TREE_OP_CHILD      2
#define CSS_TREE_OP_PRECEEDED  3
    unsigned short pclasses; /* pseudo classes & elements */
#define CSS_PCLASS_FIRST_CHILD 0x0001
#define CSS_PCLASS_LINK        0x0002
#define CSS_PCLASS_VISITED     0x0004
#define CSS_PCLASS_ACTIVE      0x0008
#define CSS_PCLASS_HOVER       0x0010
#define CSS_PCLASS_FOCUS       0x0020
    /* pseudo elements */
#define CSS_PCLASS_FIRST_LINE   0x0040
#define CSS_PCLASS_FIRST_LETTER 0x0080
#define CSS_PCLASS_BEFORE       0x0100
#define CSS_PCLASS_AFTER        0x0200

    CSSIdent tag;      /* selection tag, CSS_ID_ALL means all */
    CSSIdent tag_id;   /* id */
    CSSStyleSheetAttributeEntry *attrs; /* attribute list */
    struct CSSSimpleSelector *next; /* next selector operation */
} CSSSimpleSelector;

typedef struct CSSStyleSheetEntry {
    CSSSimpleSelector sel; /* main selector */
    int media;         /* CSS2 media mask */
    CSSProperty *props; /* associated properties */
    struct CSSStyleSheetEntry *hash_next; /* hash table for next matching tag */
    struct CSSStyleSheetEntry *next; /* next entry in style sheet */
} CSSStyleSheetEntry;

#define CSS_TAG_HASH_SIZE 521

typedef struct CSSStyleSheet {
    CSSStyleSheetEntry *first_entry, **plast_entry;
    CSSStyleSheetEntry *tag_hash[CSS_TAG_HASH_SIZE];
} CSSStyleSheet;

typedef struct {
    const char *ptr;
    int line_num;
    const char *filename;
    int ignore_case; /* true if case must be ignored (convert to lower case) */
} CSSParseState;

CSSStyleSheet *css_new_style_sheet(void);
void css_parse_style_sheet_str(CSSStyleSheet *s, const char *buffer, int flags);
void css_parse_style_sheet(CSSStyleSheet *s, CSSParseState *b);
void css_free_style_sheet(CSSStyleSheet *s);
void css_dump_style_sheet(CSSStyleSheet *s);
void css_merge_style_sheet(CSSStyleSheet *s, CSSStyleSheet *a);

CSSProperty *css_parse_properties(CSSParseState *b, const char *props_str);
void css_add_prop_values(CSSProperty ***last_prop, 
                         int property_index, 
                         int nb_values, CSSPropertyValue *val_ptr);
void css_add_prop(CSSProperty ***last_prop, 
                  int property_index, CSSPropertyValue *val_ptr);
void css_add_prop_unit(CSSProperty ***last_prop, 
                       int property_index, int type, int val);
void css_add_prop_int(CSSProperty ***last_prop, 
                      int property_index, int val);
CSSStyleSheetEntry *add_style_entry(CSSStyleSheet *s,
                                    CSSSimpleSelector *ss,
                                    int media);
void add_attribute(CSSStyleSheetAttributeEntry ***last_attr,
                   CSSIdent attr, int op, const char *value);
int get_font_size(int i);

/* CSS creating, layout and display */

struct CSSContext;

#define PROPS_HASH_SIZE 521
#define PROPS_SIZE ((int)&((CSSState *)0)->hash_next)

typedef struct CSSCounterValue {
    CSSIdent counter_id;
    int value;
    struct CSSCounterValue *prev;
} CSSCounterValue;

typedef struct CSSContext {
    CSSStyleSheet *style_sheet;
    QEditScreen *screen;
    /* various default settings */
    /* XXX: use style sheet ? */
    int selection_bgcolor;
    int selection_fgcolor;
    int default_bgcolor;
    
    /* edit buffer */
    EditBuffer *b;
    /* selection handling */
    int selection_start, selection_end;
    /* private content */
    int media;     /* initialized when creating CSSContext */
    int px_size; /* px unit size, in displayed pixels (CSS_LENGTH_FRAC_BASE) */
    int dots_per_inch; /* convert from inch to displayed pixels */
    int bg_drawn; /* temporary for css_display */
    CSSRect bg_rect; /* temporary for css_display */
    CSSAbortFunc *abort_func;
    void *abort_opaque;
    int nb_props; /* statistics */

    /* only used during css_compute() */
    CSSCounterValue *counter_stack_ptr;
    CSSCounterValue *counter_stack_base;

    /* css attributes for the boxes are shared here */
    CSSState *hash_props[PROPS_HASH_SIZE]; 
} CSSContext;

/* document managing */

void css_init(void);

CSSContext *css_new_document(QEditScreen *screen,
                             EditBuffer *b);
void css_delete_document(CSSContext *s);

int css_compute(CSSContext *s, CSSBox *box);
int css_layout(CSSContext *s, CSSBox *box, int width,
               CSSAbortFunc *abort_func, void *abort_opaque);
void css_display(CSSContext *s, CSSBox *box, 
                 CSSRect *clip_box, int dx, int dy);

/* cursor/edition handling */
int box_get_text(CSSContext *s,
                 unsigned int *line_buf, int max_size, 
                 int *offsets, CSSBox *box);
int css_get_cursor_pos(CSSContext *s, CSSBox *box, 
                       CSSBox **box_ptr, int *x0_ptr, int *y0_ptr,
                       CSSRect *cursor_ptr, int *dir_ptr, 
                       int offset);
int css_get_offset_pos(CSSContext *s, CSSBox *box, int xc, int dir);

typedef int (*CSSIterateFunc)(void *opaque, CSSBox *box, int x0, int y0);
int css_box_iterate(CSSContext *s, CSSBox *box, void *opaque, 
                    CSSIterateFunc iterate_func);


/* box tree handling */
CSSBox *css_new_box(CSSIdent tag, CSSAttribute *attrs);
CSSBox *css_add_box(CSSBox *parent_box, CSSBox *box);
void css_delete_box(CSSBox *box);
void css_set_text_buffer(CSSBox *box,
                         int offset1, int offset2, int eol);
void css_set_text_string(CSSBox *box, const char *string);
void css_make_child_box(CSSBox *box);
void css_set_child_box(CSSBox *parent_box, CSSBox *box);

/* box tree display (debug) */
void css_dump_box(CSSBox *box, int level);
void css_dump(CSSBox *box);

/* XML parser */
struct XMLState;
typedef struct XMLState XMLState;

#define XML_HTML        0x0001 /* enable html tag interpretation which cannot
                                  be specified in a stylesheet */
#define XML_IGNORE_CASE 0x0002 /* ignore case for both xml/css tag parsing */
#define XML_DOCBOOK     0x0004 /* programlisting is handled as PRE */
#define XML_HTML_SYNTAX 0x0008 /* modify xml parser to accept HTML syntax */

XMLState *xml_begin(CSSStyleSheet *style_sheet, int flags,
                    CSSAbortFunc *abort_func, void *abort_opaque, 
                    const char *filename, QECharset *charset);
int xml_parse(XMLState *s, char *buf, int buf_len);
CSSBox *xml_end(XMLState *s);

CSSBox *xml_parse_buffer(EditBuffer *b, int offset_start, int offset_end, 
                         CSSStyleSheet *style_sheet, int flags,
                         CSSAbortFunc *abort_func, void *abort_opaque);
int find_entity(const char *str);
const char *find_entity_str(int code);
#endif

/* The following functions must be provided by the user */

/* error reporting during parsing */
void html_error(int line_num, const char *fmt, va_list ap);

void css_error(const char *filename, int line_num, const char *msg);

/* file handling (for external scripts/css) */
struct CSSFile;
typedef struct CSSFile CSSFile;

CSSFile *css_open(CSSContext *s, const char *filename);
int css_filesize(CSSFile *f);
int css_read(CSSFile *f, unsigned char *buf, int size);
void css_close(CSSFile *f);



typedef struct CFBContext {
    unsigned char *base;
    int bpp;   /* number of bytes per pixel */
    int depth; /* number of color bits per pixel */
    int wrap;
    unsigned int (*get_color)(unsigned int);
    void (*draw_glyph)(QEditScreen *s1,
                       int x1, int y1, int w, int h, QEColor color,
                       unsigned char *glyph, int glyph_wrap);
} CFBContext;

int cfb_init(QEditScreen *s, 
             void *base, int wrap, int depth, const char *font_path);

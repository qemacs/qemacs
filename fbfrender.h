
/* glyph cache */
typedef struct GlyphCache {
    struct GlyphCache *hash_next;
    struct GlyphCache *prev, *next;
    void *private; /* private data available for the driver, initialized to NULL */
    /* font info */
    short size; /* font size */
    unsigned short style; /* font style */
    short w, h;   /* glyph bitmap size */
    short x, y;     /* glyph bitmap offset */
    unsigned short index; /* glyph index */
    unsigned short data_size;
    short xincr;  /* glyph x increment */
    unsigned char is_fallback; /* true if fallback glyph */
    unsigned char data[0];
} GlyphCache;

void fbf_text_metrics(QEditScreen *s, QEFont *font, 
                      QECharMetrics *metrics,
                      const unsigned int *str, int len);
GlyphCache *decode_cached_glyph(QEditScreen *s, QEFont *font, int code);
QEFont *fbf_open_font(QEditScreen *s, int style, int size);
void fbf_close_font(QEditScreen *s, QEFont *font);

int fbf_render_init(const char *font_path);
void fbf_render_cleanup(void);

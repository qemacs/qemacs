#ifdef STYLE_DEF

    /* root style, must be complete */
    STYLE_DEF(QE_STYLE_DEFAULT, "default", /* #f8d8b0 on black */
              QERGB(0xf8, 0xd8, 0xb0), QERGB(0x00, 0x00, 0x00), QE_FONT_FAMILY_FIXED, 12)

    /* system styles */
    STYLE_DEF(QE_STYLE_MODE_LINE, "mode-line", /* black on grey88 */
              QERGB(0x00, 0x00, 0x00), QERGB(0xe0, 0xe0, 0xe0), 0, 0)
    STYLE_DEF(QE_STYLE_WINDOW_BORDER, "window-border", /* black on grey88 */
              QERGB(0x00, 0x00, 0x00), QERGB(0xe0, 0xe0, 0xe0), 0, 0)
    STYLE_DEF(QE_STYLE_MINIBUF, "minibuf", /* yellow */
              QERGB(0xff, 0xff, 0x00), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_STATUS, "status", /* yellow */
              QERGB(0xff, 0xff, 0x00), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_GUTTER, "gutter", /* #f84400 on grey25 */
              QERGB(0xf8, 0x44, 0x00), QERGB(0x3f, 0x3f, 0x3f), 0, 0)

    /* default style for HTML/CSS2 pages */
    STYLE_DEF(QE_STYLE_CSS_DEFAULT, "css-default", /* black on grey74 */
              QERGB(0x00, 0x00, 0x00), QERGB(0xbb, 0xbb, 0xbb), QE_FONT_FAMILY_SERIF, 12)

    /* coloring styles */
    STYLE_DEF(QE_STYLE_HIGHLIGHT, "highlight", /* black on #f8d8b0 */
              QERGB(0x00, 0x00, 0x00), QERGB(0xf8, 0xd8, 0xb0), 0, 0)
    STYLE_DEF(QE_STYLE_SELECTION, "selection", /* white on blue */
              QERGB(0xff, 0xff, 0xff), QERGB(0x00, 0x00, 0xff), 0, 0)

    /* Generic syntax coloring styles */
    STYLE_DEF(QE_STYLE_COMMENT, "comment", /* #f84400 */
              QERGB(0xf8, 0x44, 0x00), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_PREPROCESS, "preprocess", /* cyan */
              QERGB(0x00, 0xff, 0xff), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_STRING, "string", /* #f8a078 */
              QERGB(0xf8, 0xa0, 0x78), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_STRING_Q, "string-q", /* #f8a078 */
              QERGB(0xf8, 0xa0, 0x78), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_KEYWORD, "keyword", /* cyan */
              QERGB(0x00, 0xff, 0xff), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_NUMBER, "number", /* #f8d8b0 */
              QERGB(0xf8, 0xd8, 0xb0), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_FUNCTION, "function", /* #80ccf0 */
              QERGB(0x80, 0xcc, 0xf0), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_VARIABLE, "variable", /* #e8dc80 */
              QERGB(0xe8, 0xdc, 0x80), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_TYPE, "type", /* #98f898 */
              QERGB(0x98, 0xf8, 0x98), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_TAG, "tag", /* cyan */
              QERGB(0x00, 0xff, 0xff), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_CSS, "css", /* #98f898 */
              QERGB(0x98, 0xf8, 0x98), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_ERROR, "error", /* red */
              QERGB(0xff, 0x00, 0x00), COLOR_TRANSPARENT, 0, 0)

    /* popup / region styles */
    STYLE_DEF(QE_STYLE_REGION_HILITE, "region-hilite", /* black on #80f0f0 */
              QERGB(0x00, 0x00, 0x00), QERGB(0x80, 0xf0, 0xf0), 0, 0)
    STYLE_DEF(QE_STYLE_SEARCH_HILITE, "search-hilite", /* black on teal */
              QERGB(0x00, 0x00, 0x00), QERGB(0x00, 0x80, 0x80), 0, 0)
    STYLE_DEF(QE_STYLE_SEARCH_MATCH, "search-match", /* grey88 on #f000f0 */
              QERGB(0xe0, 0xe0, 0xe0), QERGB(0xf0, 0x00, 0xf0), 0, 0)
    STYLE_DEF(QE_STYLE_BLANK_HILITE, "blank-hilite", /* black on red */
              QERGB(0x00, 0x00, 0x00), QERGB(0xff, 0x00, 0x00), 0, 0)

    /* HTML coloring styles */
    STYLE_DEF(QE_STYLE_HTML_COMMENT, "html-comment", /* #f84400 */
              QERGB(0xf8, 0x44, 0x00), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_HTML_STRING, "html-string", /* #f8a078 */
              QERGB(0xf8, 0xa0, 0x78), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_HTML_ENTITY, "html-entity", /* #e8dc80 */
              QERGB(0xe8, 0xdc, 0x80), COLOR_TRANSPARENT, 0, 0)
    STYLE_DEF(QE_STYLE_HTML_TAG, "html-tag", /* #80ccf0 */
              QERGB(0x80, 0xcc, 0xf0), COLOR_TRANSPARENT, 0, 0)

#endif

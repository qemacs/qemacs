    /* root style, must be complete */
    STYLE_DEF(QE_STYLE_DEFAULT, "default",
              QERGB(0xf8, 0xd8, 0xb0), QERGB(0x00, 0x00, 0x00),
              QE_FAMILY_FIXED, 12)

    /* system styles */
    STYLE_DEF(QE_STYLE_MODE_LINE, "mode-line",
              QERGB(0x00, 0x00, 0x00), QERGB(0xe0, 0xe0, 0xe0),
              0, 0)
    STYLE_DEF(QE_STYLE_MINIBUF, "minibuf",
              QERGB(0xff, 0xff, 0x00), COLOR_TRANSPARENT,
              0, 0)
    STYLE_DEF(QE_STYLE_STATUS, "status",
              QERGB(0xff, 0xff, 0x00), COLOR_TRANSPARENT,
              0, 0)

    /* default style for HTML/CSS2 pages */
    STYLE_DEF(QE_STYLE_CSS_DEFAULT, "css-default",
              QERGB(0x00, 0x00, 0x00), QERGB(0xbb, 0xbb, 0xbb),
              QE_FAMILY_SERIF, 12)

    /* coloring styles */
    STYLE_DEF(QE_STYLE_HIGHLIGHT, "highlight",
              QERGB(0x00, 0x00, 0x00), QERGB(0xf8, 0xd8, 0xb0),
              0, 0)
    STYLE_DEF(QE_STYLE_SELECTION, "selection",
              QERGB(0xff, 0xff, 0xff), QERGB(0x00, 0x00, 0xff),
              0, 0)
    STYLE_DEF(QE_STYLE_COMMENT, "comment",
              QERGB(0xf8, 0x44, 0x00), COLOR_TRANSPARENT,
              0, 0)
    STYLE_DEF(QE_STYLE_PREPROCESS, "preprocess",
              QERGB(0x00, 0xff, 0xff), COLOR_TRANSPARENT,
              0, 0)
    STYLE_DEF(QE_STYLE_STRING, "string",
              QERGB(0xf8, 0xa0, 0x78), COLOR_TRANSPARENT,
              0, 0)
    STYLE_DEF(QE_STYLE_KEYWORD, "keyword",
              QERGB(0x00, 0xff, 0xff), COLOR_TRANSPARENT,
              0, 0)
    STYLE_DEF(QE_STYLE_FUNCTION, "function",
              QERGB(0x80, 0xcc, 0xf0), COLOR_TRANSPARENT,
              0, 0)
    STYLE_DEF(QE_STYLE_VARIABLE, "variable",
              QERGB(0xe8, 0xdc, 0x80), COLOR_TRANSPARENT,
              0, 0)
    STYLE_DEF(QE_STYLE_TYPE, "type",
              QERGB(0x98, 0xf8, 0x98), COLOR_TRANSPARENT,
              0, 0)
    STYLE_DEF(QE_STYLE_TAG, "tag",
              QERGB(0x00, 0xff, 0xff), COLOR_TRANSPARENT,
              0, 0)
    STYLE_DEF(QE_STYLE_CSS, "css",
              QERGB(0x98, 0xf8, 0x98), COLOR_TRANSPARENT,
              0, 0)

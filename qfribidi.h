
typedef int FriBidiChar;

typedef enum {
    /* do not change the order of these constants since 'property_val'
       depends on them */
    FRIBIDI_TYPE_LTR,		/* Strong left to right */
    FRIBIDI_TYPE_RTL,		/* Right to left characters */
    FRIBIDI_TYPE_WL,		/* Weak left to right */
    FRIBIDI_TYPE_WR,		/* Weak right to left */
    FRIBIDI_TYPE_EN,		/* European digit */
    FRIBIDI_TYPE_ES,		/* European number separator */
    FRIBIDI_TYPE_ET,		/* European number terminator */
    FRIBIDI_TYPE_AN,		/* Arabic digit */
    FRIBIDI_TYPE_CS,		/* Common Separator */
    FRIBIDI_TYPE_BS,		/* Block separator */
    FRIBIDI_TYPE_SS,		/* Segment separator */
    FRIBIDI_TYPE_WS,		/* Whitespace */
    FRIBIDI_TYPE_AL,		/* Arabic characters */
    FRIBIDI_TYPE_NSM,		/* Non spacing mark */
    FRIBIDI_TYPE_BN,
    FRIBIDI_TYPE_ON,		/* Other Neutral */
    FRIBIDI_TYPE_LRE,		/* Left-To-Right embedding */
    FRIBIDI_TYPE_RLE,		/* Right-To-Left embedding */
    FRIBIDI_TYPE_PDF,		/* Pop directional override */
    FRIBIDI_TYPE_LRO,		/* Left-To-Right override */
    FRIBIDI_TYPE_RLO,		/* Right-To-Left override */

    /* The following are only used internally */
    FRIBIDI_TYPE_SOT,
    FRIBIDI_TYPE_EOT,
    FRIBIDI_TYPE_N,
    FRIBIDI_TYPE_E,
    FRIBIDI_TYPE_CTL,		/* Control units */
    FRIBIDI_TYPE_EO,		/* Control units */
    FRIBIDI_TYPE_NULL,		/* type record is to be deleted */
    FRIBIDI_TYPE_L = FRIBIDI_TYPE_LTR,
    FRIBIDI_TYPE_R = FRIBIDI_TYPE_RTL,
    FRIBIDI_TYPE_CM = FRIBIDI_TYPE_ON + 2,
} FriBidiCharType;

/*======================================================================
// Typedef for the run-length list.
//----------------------------------------------------------------------*/
typedef struct _TypeLink {
    FriBidiCharType type;
    int pos;
    int len;
    int level;
} TypeLink;

FriBidiCharType fribidi_get_type(FriBidiChar ch);
FriBidiCharType fribidi_get_type_test(FriBidiChar ch);
FriBidiChar fribidi_get_mirror_char(FriBidiChar ch);

void fribidi_analyse_string(TypeLink * type_rl_list,
                            FriBidiCharType * pbase_dir,
                            int *pmax_level);


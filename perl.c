/*
 * Perl Source mode for QEmacs.
 *
 * Copyright (c) 2000-2007 Charlie Gordon.
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

static const char *perl_mode_extensions = "pl|perl";

/*---------------- Perl colors ----------------*/

#define PERL_TEXT	QE_STYLE_DEFAULT
#define PERL_COMMENT	QE_STYLE_COMMENT
#define PERL_STRING	QE_STYLE_STRING
#define PERL_REGEX	QE_STYLE_STRING
#define PERL_DELIM	QE_STYLE_KEYWORD
#define PERL_KEYWORD	QE_STYLE_KEYWORD
#define PERL_VAR	QE_STYLE_VARIABLE
#define PERL_NUMBER	QE_STYLE_NUMBER

#define IN_STRING1	0x01	/* single quote */
#define IN_STRING2	0x02	/* double quote */
#define IN_FORMAT	0x04	/* format = ... */
#define IN_INPUT	0x08

/* CG: bogus if multiple regions are colorized */
static unsigned int perl_eos[100];
static int perl_eos_len;

static int perl_var(const unsigned int *str, int j, int n)
{
    n = n;

    if (qe_isdigit(str[j]))
	return j;
    for (; j < n; j++) {
	if (qe_isalnum(str[j]) || str[j] == '_')
	    continue;
	if (str[j] == '\''
	&&  (qe_isalpha(str[j + 1]) || str[j + 1] == '_'))
	    j++;
	else
	    break;
    }
    return j;
}

static int perl_number(const unsigned int *str, int j, int n)
{
    n = n;

    if (str[j] == '0') {
	j++;
	if (str[j] == 'x' || str[j] == 'X') {
	    do j++; while (qe_isxdigit(str[j]));
	    return j;
	}
	if (str[j] >= '0' && str[j] <= '7') {
            do j++; while (str[j] >= '0' && str[j] <= '7');
	    return j;
	}
    }
    while (qe_isdigit(str[j]))
	j++;

    if (str[j] == '.')
	do j++; while (qe_isdigit(str[j]));

    if (str[j] == 'E' || str[j] == 'e') {
	j++;
	if (str[j] == '-' || str[j] == '+')
	    j++;
	while (qe_isdigit(str[j]))
	    j++;
    }
    return j;
}

/* return offset of matching delimiter or end of string */
static int perl_string(const unsigned int *str, unsigned int delim, int j, int n)
{
    for (; j < n; j++) {
	if (str[j] == '\\')
	    j++;
	else
	if (str[j] == delim)
	    return j;
    }
    return j;
}

static void perl_colorize_line(unsigned int *str, int n, int *statep, int state_only)
{
    int i = 0, c, c1, c2, j = i, s1, s2, delim = 0;
    int colstate = *statep;

    if (colstate & (IN_STRING1|IN_STRING2)) {
	delim = (colstate & IN_STRING1) ? '\'' : '\"';
	i = perl_string(str, delim, j, n);
	if (i < n) {
	    i++;
	    colstate &= ~(IN_STRING1|IN_STRING2);
	}
	set_color(str + j, str + i, PERL_STRING);
    } else
    if (colstate & IN_FORMAT) {
	i = n;
	set_color(str + j, str + i, PERL_STRING);
	if (n == 1 && str[0] == '.')
	    colstate &= ~IN_FORMAT;
    }
    if (colstate & IN_INPUT) {
	//vdm_noRetrievalKey = 1;
	i = n;
	set_color(str + j, str + i, PERL_STRING);
	if (n == perl_eos_len && !umemcmp(perl_eos, str, n))
	    colstate &= ~IN_INPUT;
    }
    while (i < n) {
	j = i + 1;
	c1 = str[j];
	switch (c = str[i]) {
	case '$':
	    if (c1 == '^' && qe_isalpha(str[i + 2])) {
		j = i + 3;
		goto keyword;
	    }
	    if (c1 == '#'
	    &&  (qe_isalpha(str[i + 2]) || str[i + 2] == '_'))
		j++;
	    else
	    if (!qe_isalpha(c1) && c1 != '_') {
		/* Special variable */
		j = i + 2;
		goto keyword;
	    }
	    /* FALL THRU */
	case '*':
	case '@':
	case '%':
	case '&':
	    if (j >= n)
		break;
	    s1 = perl_var(str, j, n);
	    if (s1 > j) {
		set_color(str + i, str + s1, PERL_VAR);
		i = s1;
		continue;
	    }
	    break;
	case '-':
	    if (c1 == '-') {
		i += 2;
		continue;
	    }
	    if (qe_isalpha(c1) && !qe_isalnum(str[i + 2])) {
		j = i + 2;
		goto keyword;
	    }
	    break;
	case '#':
	    set_color(str + i, str + n, PERL_COMMENT);
	    i = n;
	    continue;
	case '<':
	    if (c1 == '<') {
		/* Should check for unary context */
		s1 = i + 2;
		c2 = str[s1];
		if (c2 == '"' || c2 == '\'' || c2 == '`')
		    s2 = perl_string(str, c2, ++s1, n);
		else
		    s2 = perl_var(str, s1, n);
		if (s2 > s1) {
		    umemcpy(perl_eos, str + s1, s2 - s1);
		    perl_eos_len = s2 - s1;
		    colstate |= IN_INPUT;
		}
		i += 2;
		continue;
	    }
	    delim = '>';
	    goto string;
	case '/':
	case '?':
	    /* Should check for unary context */
	    /* parse regex */
	    s1 = perl_string(str, c, j, n);
	    if (s1 >= n)
		break;
	    set_color1(str + i, PERL_DELIM);
	    set_color(str + i + 1, str + s1, PERL_REGEX);
	    i = s1;
	    while (++i < n && qe_isalpha(str[i]))
		continue;
	    set_color(str + s1, str + i, PERL_DELIM);
	    continue;
	case '\'':
	case '`':
	case '"':
	    delim = c;
	string:
	    /* parse string const */
	    s1 = perl_string(str, delim, j, n);
	    if (s1 >= n) {
		if (c == '\'') {
		    set_color(str + i, str + n, PERL_STRING);
		    i = n;
		    colstate |= IN_STRING1;
		    continue;
		}
		if (c == '\"') {
		    set_color(str + i, str + n, PERL_STRING);
		    i = n;
		    colstate |= IN_STRING2;
		    continue;
		}
		break;
	    }
	    s1++;
	    set_color(str + i, str + s1, PERL_STRING);
	    i = s1;
	    continue;
	default:
	    if (qe_isdigit(c)) {
		j = perl_number(str, i, n);
		set_color(str + i, str + j, PERL_NUMBER);
		i = j;
		continue;
	    }
	    if (!qe_isalpha(c) && c != '_')
		break;

	    j = perl_var(str, i, n);
	    if (j == i)
		break;

	    if (j >= n)
		goto keyword;

	    /* Should check for context */
	    if ((j == i + 1 && (c == 'm' || c == 'q'))
	    ||  (j == i + 2 && c == 'q' && (c1 == 'q' || c1 == 'x'))) {
		s1 = perl_string(str, str[j], j + 1, n);
		if (s1 >= n)
		    goto keyword;
		set_color(str + i, str + j + 1, PERL_DELIM);
		set_color(str + j + 1, str + s1, PERL_REGEX);
		i = s1;
		while (++i < n && qe_isalpha(str[i]))
		    continue;
		set_color(str + s1, str + i, PERL_DELIM);
		continue;
	    }
	    /* Should check for context */
	    if ((j == i + 1 && (c == 's' /* || c == 'y' */))
	    ||  (j == i + 2 && c == 't' && c1 == 'r')) {
		s1 = perl_string(str, str[j], j + 1, n);
		if (s1 >= n)
		    goto keyword;
		s2 = perl_string(str, str[j], s1 + 1, n);
		if (s2 >= n)
		    goto keyword;
		set_color(str + i, str + j + 1, PERL_DELIM);
		set_color(str + j + 1, str + s1, PERL_REGEX);
		set_color1(str + s1, PERL_DELIM);
		set_color(str + s1 + 1, str + s2, PERL_REGEX);
		i = s2;
		while (++i < n && qe_isalpha(str[i]))
		    continue;
		set_color(str + s2, str + i, PERL_DELIM);
		continue;
	    }
	keyword:
	    if (j - i == 6 && ustristart(str + i, "format", NULL))
		colstate |= IN_FORMAT;
	    set_color(str + i, str + j, PERL_KEYWORD);
	    i = j;
	    continue;
	}
	i++;
	continue;
    }
    *statep = colstate;
}

#undef PERL_TEXT
#undef PERL_COMMENT
#undef PERL_STRING
#undef PERL_REGEX
#undef PERL_DELIM
#undef PERL_KEYWORD
#undef PERL_VAR
#undef PERL_NUMBER

#undef IN_STRING1
#undef IN_STRING2
#undef IN_FORMAT
#undef IN_INPUT

static int perl_mode_probe(ModeProbeData *p)
{
    /* just check file extension */
    if (match_extension(p->filename, perl_mode_extensions))
	return 80;
    
    return 0;
}

static int perl_mode_init(EditState *s, ModeSavedData *saved_data)
{
    int ret;

    ret = text_mode_init(s, saved_data);
    if (ret)
        return ret;
    set_colorize_func(s, perl_colorize_line);
    return ret;
}

/* specific perl commands */
static CmdDef perl_commands[] = {
    CMD_DEF_END,
};

static ModeDef perl_mode;

static int perl_init(void)
{
    /* c mode is almost like the text mode, so we copy and patch it */
    memcpy(&perl_mode, &text_mode, sizeof(ModeDef));
    perl_mode.name = "Perl";
    perl_mode.mode_probe = perl_mode_probe;
    perl_mode.mode_init = perl_mode_init;

    qe_register_mode(&perl_mode);
    qe_register_cmd_table(perl_commands, "Perl");

    return 0;
}

qe_module_init(perl_init);

/*
 * SQL language mode for QEmacs.
 *
 * Copyright (c) 2000-2025 Charlie Gordon.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qe.h"

/*---------------- SQL script coloring ----------------*/

enum {
    IN_SQL_COMMENT = 1,
};

enum {
    SQL_STYLE_TEXT =       QE_STYLE_DEFAULT,
    SQL_STYLE_COMMENT =    QE_STYLE_COMMENT,
    SQL_STYLE_STRING =     QE_STYLE_STRING,
    SQL_STYLE_KEYWORD =    QE_STYLE_KEYWORD,
    SQL_STYLE_TYPE =       QE_STYLE_TYPE,
    SQL_STYLE_IDENTIFIER = QE_STYLE_DEFAULT,
    SQL_STYLE_PREPROCESS = QE_STYLE_PREPROCESS,
};

static char const sql_keywords[] = {
    "abs|acos|add|aes_decrypt|aes_encrypt|after|all|alter|analyse|analyze|"
    "and|as|asc|ascii|asin|atan|atan2|auto_increment|avg|backup|begin|"
    "benchmark|between|bin|binlog|bit_and|bit_count|bit_length|bit_or|"
    "bit_xor|both|btree|by|call|case|cast|ceil|ceiling|change|character|"
    "character_length|char_length|check|checksum|clob|clock|coalesce|"
    "collate|column|columns|comment|commit|compressed|concat|concat_ws|"
    "concurrent|constraint|contents|controlfile|conv|convert|cos|cot|"
    "count|crc32|crc64|create|current_date|current_time|current_timestamp|"
    "current_user|data|database|databases|declare|default|degrees|delayed|"
    "delete|desc|describe|directory|disable|discard|div|do|drop|dump|elt|"
    "enable|enclosed|end|engine|enum|escaped|event|events|execute|exists|"
    "exp|explain|export_set|fail|false|field|fields|find_in_set|first|"
    "floor|for|foreign|format|found_rows|from|full|fulltext|function|"
    "global|go|grant|greatest|group_concat|handler|hash|having|help|hex|"
    "high_priority|hsieh_hash|if|ifnull|ignore|import|in|index|inet|infile|"
    "insert|install|instr|interval|into|is|isnull|iterate|jenkins_hash|key|"
    "keys|last|last_insert_id|lcase|leading|least|leave|left|length|like|"
    "limit|lines|ln|load|load_file|local|localtime|localtimestamp|locate|"
    "lock|log|log10|log2|logs|loop|lower|low_priority|lpad|ltrim|make_set|"
    "max|md5|md5_bin|memory|mid|min|mod|modify|no|none|not|now|null|nullif|"
    "oct|off|offset|on|optionally|or|ord|order|outfile|password|pi|pid|pow|"
    "power|prepare|primary|print|procedure|quote|radians|rand|recno|"
    "release|rename|repair|repeat|replace|restore|return|reverse|revoke|"
    "right|rollback|round|rpad|rtree|rtrim|rule|savepoint|schema|select|"
    "sequence|serial|server|session|set|sha|sha1|sha128_bin|sha224_bin|"
    "sha256_bin|sha384_bin|sha512_bin|show|sign|signed|sin|soundex|source|"
    "space|spatial|sql_calc_found_rows|sqrt|start|starting|stats|std|"
    "stddev|stddev_pop|stddev_samp|strcmp|string|structure|substr|"
    "substring|substring_index|sum|table|tables|tan|temporary|terminated|"
    "time|timestamp|timings|to|trailing|transaction|trigger|trim|true|"
    "truncate|type|ucase|unhex|uninstall|unique|unix_timestamp|unknown|"
    "unlock|update|upper|use|user|using|utf8|value|values|varbinary|"
    "variables|variance|var_pop|var_samp|verbose|version_comment|view|"
    "when|where|while|xml|year|yes|"
    "pragma|"
    "adddate|addtime|curdate|curtime|date_add|date_sub|date_format|"
    "datediff|day|dayname|dayofmonth|dayofweek|dayofyear|extract|"
    "from_days|from_unixtime|get_format|hour|last_day|makedate|maketime|"
    "microsecond|minute|month|monthname|period_add|period_diff|quarter|"
    "sec_to_time|second|str_to_date|subdate|subtime|sysdate|timediff|"
    "time_format|time_to_sec|to_days|utc_date|utc_time|utc_timestamp|"
    "week|weekday|weekofyear|yearweek|second_microsecond|"
    "minute_microsecond|minute_second|hour_microsecond|hour_second|"
    "hour_minute|day_microsecond|day_second|day_minute|day_hour|"
    "year_month|"
};

static char const sql_types[] = {
    "bigint|binary|bit|blob|bool|char|counter|date|datetime|dec|decimal|"
    "double|fixed|float|int|int16|int24|int32|int48|int64|int8|integer|"
    "largeint|long|longblob|longtext|mediumblob|mediumint|mediumtext|"
    "memo|number|numeric|real|smallint|text|tinyblob|tinyint|tinytext|"
    "uint16|uint24|uint32|uint48|uint64|uint8|ulong|unsigned|varchar|"
    "varchar2|"
};

static void sql_colorize_line(QEColorizeContext *cp,
                              const char32_t *str, int n,
                              QETermStyle *sbuf, ModeDef *syn)
{
    char kbuf[16];
    int i = 0, start = i, style;
    char32_t c;
    int state = cp->colorize_state;

    if (state & IN_SQL_COMMENT)
        goto parse_c_comment;

    while (i < n) {
        start = i;
        c = str[i++];
        switch (c) {
        case '/':
            if (str[i] == '/')
                goto line_comment;
            if (str[i] == '*') {
                /* normal comment */
                i++;
            parse_c_comment:
                state |= IN_SQL_COMMENT;
                for (; i < n; i++) {
                    if (str[i] == '*' && str[i + 1] == '/') {
                        i += 2;
                        state &= ~IN_SQL_COMMENT;
                        break;
                    }
                }
                goto comment;
            }
            break;
        case '-':
            if (str[i] == '-')
                goto line_comment;
            break;
        case '#':
        line_comment:
            i = n;
        comment:
            SET_STYLE(sbuf, start, i, SQL_STYLE_COMMENT);
            continue;
        case '\'':
        case '\"':
        case '`':
            /* parse string const */
            for (; i < n; i++) {
                /* FIXME: Should parse strings more accurately */
                if (str[i] == '\\' && i + 1 < n) {
                    i++;
                    continue;
                }
                if (str[i] == c) {
                    i++;
                    break;
                }
            }
            style = SQL_STYLE_STRING;
            if (c == '`')
                style = SQL_STYLE_IDENTIFIER;
            SET_STYLE(sbuf, start, i, style);
            continue;
        default:
            break;
        }
        /* parse identifiers and keywords */
        if (qe_isalpha_(c)) {
            i += ustr_get_identifier_lc(kbuf, countof(kbuf), c, str, i, n);
            if (strfind(syn->keywords, kbuf)) {
                SET_STYLE(sbuf, start, i, SQL_STYLE_KEYWORD);
                continue;
            }
            if (strfind(syn->types, kbuf)) {
                SET_STYLE(sbuf, start, i, SQL_STYLE_TYPE);
                continue;
            }
            SET_STYLE(sbuf, start, i, SQL_STYLE_IDENTIFIER);
            continue;
        }
    }
    cp->colorize_state = state;
}

static int sql_mode_probe(ModeDef *mode, ModeProbeData *pd)
{
    const char *p = cs8(pd->buf);

    if (strstart(p, "PRAGMA foreign_keys=OFF;", NULL)
    ||  strstart(p, "-- phpMyAdmin SQL Dump", NULL)) {
        return 80;
    }
    if (match_extension(pd->filename, mode->extensions))
        return 60;

    return 1;
}

ModeDef sql_mode = {
    .name = "SQL",
    .extensions = "sql|Sql|mysql|sqlite|sqlplus|rdb|xdb|db",
    .mode_probe = sql_mode_probe,
    .keywords = sql_keywords,
    .types = sql_types,
    .colorize_func = sql_colorize_line,
};

static int sql_init(QEmacsState *qs)
{
    qe_register_mode(qs, &sql_mode, MODEF_SYNTAX);
    return 0;
}

qe_module_init(sql_init);

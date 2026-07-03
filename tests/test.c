/*
 * Copyright (c) 2026 Liquidaty. MIT License.
 *
 * Unit tests for the yatl TOON SAX parser. Each callback appends a compact
 * token to a global buffer; a test feeds TOON and compares the resulting event
 * string against an expected encoding. Inputs are run both whole and one byte
 * at a time to exercise chunk-boundary buffering.
 *
 * Event encoding: '{' '}' '[' ']' containers; 'k(...)' key; 's(...)' string;
 * '~' null; 'T'/'F' bool; '#<int>'; '%<double>'; 'N(...)' verbatim number.
 */
#include <yatl/yatl_parse.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_buf[8192];
static size_t g_len;

static void put(const char *s, size_t n) {
    if (g_len + n < sizeof g_buf) { memcpy(g_buf + g_len, s, n); g_len += n; }
}
static void puts1(const char *s) { put(s, strlen(s)); }
static void wrap(char tag, const unsigned char *s, size_t n) {
    char t[2] = { tag, '(' };
    put(t, 2); put((const char *)s, n); puts1(")");
}

static int c_null(void *c)            { (void)c; puts1("~"); return 1; }
static int c_bool(void *c, int b)     { (void)c; puts1(b ? "T" : "F"); return 1; }
static int c_int(void *c, long long v){ char t[32]; (void)c; snprintf(t, sizeof t, "#%lld", v); puts1(t); return 1; }
static int c_dbl(void *c, double v)   { char t[32]; (void)c; snprintf(t, sizeof t, "%%%g", v); puts1(t); return 1; }
static int c_str(void *c, const unsigned char *s, size_t n) { (void)c; wrap('s', s, n); return 1; }
static int c_key(void *c, const unsigned char *s, size_t n) { (void)c; wrap('k', s, n); return 1; }
static int c_smap(void *c)  { (void)c; puts1("{"); return 1; }
static int c_emap(void *c)  { (void)c; puts1("}"); return 1; }
static int c_sarr(void *c)  { (void)c; puts1("["); return 1; }
static int c_earr(void *c)  { (void)c; puts1("]"); return 1; }

static const yatl_callbacks CB = {
    c_null, c_bool, c_int, c_dbl, NULL, c_str,
    c_smap, c_key, c_emap, c_sarr, c_earr, NULL
};

static int fails = 0, total = 0;

/* Parse `in` (length len) with optional config flag, chunk size `chunk`
 * (0 == whole). Returns final status; events land in g_buf. */
static yatl_status run_cfg(const char *in, size_t len, size_t chunk,
                           yatl_option opt, const yatl_callbacks *cb) {
    yatl_handle h = yatl_alloc(cb, NULL, NULL);
    yatl_status st = yatl_status_ok;
    size_t i;
    g_len = 0; g_buf[0] = 0;
    if (opt) yatl_config(h, opt, 1);
    if (chunk == 0) chunk = len ? len : 1;
    for (i = 0; i < len && st == yatl_status_ok; i += chunk) {
        size_t n = (i + chunk > len) ? len - i : chunk;
        st = yatl_parse(h, (const unsigned char *)in + i, n);
    }
    if (st == yatl_status_ok) st = yatl_complete_parse(h);
    g_buf[g_len] = 0;
    yatl_free(h);
    return st;
}

/* Expect success and an exact event string, both whole and byte-by-byte. */
static void ok(const char *name, const char *in, const char *expect) {
    size_t len = strlen(in);
    yatl_status st;
    total++;
    st = run_cfg(in, len, 0, 0, &CB);
    if (st != yatl_status_ok || strcmp(g_buf, expect) != 0) {
        fails++;
        printf("FAIL %s: status=%d\n  got:    %s\n  expect: %s\n", name, st, g_buf, expect);
        return;
    }
    st = run_cfg(in, len, 1, 0, &CB);   /* one byte at a time */
    if (st != yatl_status_ok || strcmp(g_buf, expect) != 0) {
        fails++;
        printf("FAIL %s (chunked): status=%d\n  got:    %s\n  expect: %s\n", name, st, g_buf, expect);
    }
}

/* Expect a parse error. */
static void err(const char *name, const char *in) {
    yatl_status st;
    total++;
    st = run_cfg(in, strlen(in), 0, 0, &CB);
    if (st != yatl_status_error) {
        fails++;
        printf("FAIL %s: expected error, got status=%d (%s)\n", name, st, g_buf);
    }
}

/* ----- a cancel callback: stop on the first string ----- */
static int cancel_str(void *c, const unsigned char *s, size_t n) { (void)c; (void)s; (void)n; return 0; }
/* ----- a verbatim-number callback ----- */
static int num_raw(void *c, const char *s, size_t n) { (void)c; wrap('N', (const unsigned char *)s, n); return 1; }

int main(void) {
    /* primitives & objects */
    ok("obj_no_nl",   "name: Ada",                 "{k(name)s(Ada)}");
    ok("scalars",     "a: 1\nb: true\nc: null\n",  "{k(a)#1k(b)Tk(c)~}");
    ok("nested",      "a:\n  b: 1\n",              "{k(a){k(b)#1}}");
    ok("root_int",    "42",                        "#42");
    ok("root_str",    "\"hi\"",                    "s(hi)");
    ok("root_dbl",    "3.5",                        "%3.5");
    ok("empty_doc",   "",                           "{}");
    ok("empty_str",   "x: \"\"\n",                 "{k(x)s()}");

    /* arrays */
    ok("inline",      "x[3]: 1,2,3\n",             "{k(x)[#1#2#3]}");
    ok("inline_str",  "x[2]: a,b\n",               "{k(x)[s(a)s(b)]}");
    ok("empty_arr",   "x: []\n",                   "{k(x)[]}");
    ok("list_scalar", "x[2]:\n  - a\n  - b\n",     "{k(x)[s(a)s(b)]}");
    ok("list_obj",    "x[1]:\n  - k: 1\n",         "{k(x)[{k(k)#1}]}");
    ok("list_empty_obj", "x[1]:\n  -\n",           "{k(x)[{}]}");
    ok("tabular_root","[2]{x,y}:\n  1,2\n  3,4\n", "[{k(x)#1k(y)#2}{k(x)#3k(y)#4}]");

    /* delimiters */
    /* spec §11.2: splitting trims surrounding spaces and preserves empty
     * tokens (so a trailing delimiter yields a final empty string); quoted
     * tokens keep interior spaces and may carry surrounding ones. */
    ok("inline_spaces",   "x[3]: a, b , c\n",      "{k(x)[s(a)s(b)s(c)]}");
    ok("inline_empty_ws", "x[3]: a, ,c\n",         "{k(x)[s(a)s()s(c)]}");
    ok("inline_trailing", "x[2]: a,\n",            "{k(x)[s(a)s()]}");
    ok("inline_quote_ws", "x[2]: \"a,x\" , b\n",   "{k(x)[s(a,x)s(b)]}");
    ok("quoted_inner_ws", "x[1]: \" a \"\n",       "{k(x)[s( a )]}");
    ok("row_spaces",      "[1]{a,b}:\n  1, 2\n",   "[{k(a)#1k(b)#2}]");
    err("inline_trailing_count", "x[1]: a,\n");

    /* spec §12: blank lines are ignorable outside arrays, an error inside an
     * array (i.e. while a declared item/row count is still unmet) */
    ok("blank_between_fields", "a: 1\n\nb: 2\n",            "{k(a)#1k(b)#2}");
    ok("blank_nested_obj",     "a:\n  b: 1\n\n  c: 2\n",    "{k(a){k(b)#1k(c)#2}}");
    ok("blank_after_array",    "x[1]:\n  - a\n\nb: 2\n",    "{k(x)[s(a)]k(b)#2}");
    ok("trailing_newlines",    "a: 1\n\n\n",                "{k(a)#1}");
    err("blank_in_list",       "x[2]:\n  - a\n\n  - b\n");
    err("blank_in_list_sp",    "x[2]:\n  - a\n  \n  - b\n");
    err("blank_in_rows",       "[2]{a}:\n  1\n\n  2\n");
    err("blank_in_list_objs",  "x[2]:\n  - k: 1\n\n  - k: 2\n");

    ok("pipe",        "x[2|]: a|b\n",              "{k(x)[s(a)s(b)]}");
    ok("tab",         "x[2\t]: a\tb\n",            "{k(x)[s(a)s(b)]}");
    ok("tab_table",   "[1\t]{a\tb}:\n  1\t2\n",    "[{k(a)#1k(b)#2}]");

    /* strings / escapes */
    ok("escape_nl",   "m: \"a\\nb\"\n",            "{k(m)s(a\nb)}");
    ok("escape_u",    "m: \"\\u0041\"\n",          "{k(m)s(A)}");
    /* spec §7.1 escape table is exactly \" \\ \n \r \t (+ \uXXXX); decoders MUST
     * reject every other escape -- notably \b, \f and \/, which are NOT listed. */
    ok("escape_ctrl_hex", "m: \"a\\u0008b\"\n",    "{k(m)s(a\bb)}");
    err("escape_b_reject",     "m: \"a\\bb\"\n");
    err("escape_f_reject",     "m: \"a\\fb\"\n");
    err("escape_slash_reject", "m: \"a\\/b\"\n");
    /* spec §7.1 unescaped-char includes %x09: a literal HTAB inside a quoted
     * token is valid input, equivalent to \t; other raw controls stay errors. */
    ok("literal_tab_str", "m: \"a\tb\"\n",         "{k(m)s(a\tb)}");
    ok("literal_tab_key", "\"a\tb\": 1\n",         "{k(a\tb)#1}");
    err("literal_cr_str",  "m: \"a\rb\"\n");
    ok("comma_quoted","m: \"a,b\"\n",              "{k(m)s(a,b)}");
    ok("dotted_key",  "a.b.c: 1\n",                "{k(a.b.c)#1}");
    ok("neg_and_exp", "a: -5\nb: 1.5e3\n",         "{k(a)#-5k(b)%1500}");

    /* integer range: whole long long incl LLONG_MIN; overflow widens to double */
    ok("int_min",     "a: -9223372036854775808\n", "{k(a)#-9223372036854775808}");
    ok("int_max",     "a: 9223372036854775807\n",  "{k(a)#9223372036854775807}");
    ok("int_widen",   "a: 10000000000000000000\n", "{k(a)%1e+19}");

    /* errors */
    err("len_low",    "x[3]: 1,2\n");
    err("len_high",   "x[1]: 1,2\n");
    /* spec §6: bracket length requires digits, no leading zeros */
    err("len_leading_zero", "x[03]: 1,2,3\n");
    err("len_missing",      "x[|]: a|b\n");
    ok("len_zero_list",     "x[0]:\n",       "{k(x)[]}");
    /* the bare [] token is an empty array only at root or after "key: " (§9.1);
     * without the colon it is a missing-colon / malformed-header error */
    ok("root_empty_arr",    "[]",            "[]");
    err("empty_arr_no_colon",   "x[]\n");
    err("item_bare_empty",      "x[1]:\n  - []\n");
    err("root_empty_trailing",  "[]\nx: 1\n");
    err("need_quote", "x: a,b\n");
    err("bad_escape", "x: \"a\\q\"\n");
    err("trailing",   "42\nfoo: 1\n");
    err("too_many_cols", "[1]{a}:\n  1,2\n");
    err("unterminated_q", "x: \"abc\n");
    err("list_no_header", "- a\n");
    err("dbl_overflow",   "a: 1e999\n");   /* +inf, no double sink can hold it */

    /* cancel */
    {
        yatl_callbacks cc = CB; cc.yatl_string = cancel_str;
        total++;
        if (run_cfg("x: hi\n", 6, 0, 0, &cc) != yatl_status_client_canceled) {
            fails++; printf("FAIL cancel: expected client_canceled\n");
        }
    }

    /* verbatim number callback overrides integer/double */
    {
        yatl_callbacks nc = CB; nc.yatl_number = num_raw;
        total++;
        if (run_cfg("a: 12\nb: 3.4\n", 12, 0, 0, &nc) != yatl_status_ok ||
            strcmp(g_buf, "{k(a)N(12)k(b)N(3.4)}") != 0) {
            fails++; printf("FAIL number_cb: got %s\n", g_buf);
        }
    }

    /* out-of-range integer: no double sink and no error hook -> parse error;
     * a verbatim callback still carries it losslessly */
    {
        const char *big = "a: 10000000000000000000\n";
        yatl_callbacks ic = CB; ic.yatl_double = NULL;
        total++;
        if (run_cfg(big, strlen(big), 0, 0, &ic) != yatl_status_error) {
            fails++; printf("FAIL int_overflow_no_sink: expected error\n");
        }
        {
            yatl_callbacks nc = CB; nc.yatl_number = num_raw;
            total++;
            if (run_cfg(big, strlen(big), 0, 0, &nc) != yatl_status_ok ||
                strcmp(g_buf, "{k(a)N(10000000000000000000)}") != 0) {
                fails++; printf("FAIL big_int_verbatim: got %s\n", g_buf);
            }
        }
    }

    /* UTF-8 validation: invalid byte rejected by default, accepted when off */
    {
        const char *bad = "x: \"\xff\"\n";
        total++;
        if (run_cfg(bad, strlen(bad), 0, 0, &CB) != yatl_status_error) {
            fails++; printf("FAIL utf8_reject: expected error\n");
        }
        total++;
        if (run_cfg(bad, strlen(bad), 0, yatl_dont_validate_strings, &CB) != yatl_status_ok) {
            fails++; printf("FAIL utf8_allow: expected ok with validation off\n");
        }
    }

    /* lenient mode accepts an otherwise-quote-requiring bare value */
    {
        total++;
        if (run_cfg("x: a,b\n", 7, 0, yatl_lenient_scalars, &CB) != yatl_status_ok) {
            fails++; printf("FAIL lenient: expected ok\n");
        }
    }

    /* yatl_max_depth: a value argument, not a flag. Build DEPTH nested objects
     * ("a:\n  a:\n ...  a: 1\n") and check the cap is enforced at the configured
     * depth: the default (YATL_MAX_DEPTH) rejects a document past it, while a
     * raised cap accepts the same document; a lowered cap rejects a shallow one. */
    {
        enum { DEEP = YATL_MAX_DEPTH + 4 };
        char *deep = (char *)malloc((size_t)DEEP * (2 * DEEP + 8));
        size_t p = 0, i;
        for (i = 0; i < (size_t)DEEP; i++) {
            size_t j;
            for (j = 0; j < i * 2; j++) deep[p++] = ' ';
            deep[p++] = 'a'; deep[p++] = ':';
            if (i + 1 == (size_t)DEEP) { deep[p++] = ' '; deep[p++] = '1'; }
            deep[p++] = '\n';
        }
        /* default cap rejects DEEP levels */
        total++;
        { yatl_handle h = yatl_alloc(&CB, NULL, NULL);
          yatl_status st = yatl_parse(h, (const unsigned char *)deep, p);
          if (st == yatl_status_ok) st = yatl_complete_parse(h);
          if (st != yatl_status_error) { fails++; printf("FAIL max_depth_default: expected error\n"); }
          yatl_free(h); }
        /* raised cap accepts DEEP levels */
        total++;
        { yatl_handle h = yatl_alloc(&CB, NULL, NULL);
          yatl_status st;
          yatl_config(h, yatl_max_depth, (unsigned)(DEEP + 4));
          st = yatl_parse(h, (const unsigned char *)deep, p);
          if (st == yatl_status_ok) st = yatl_complete_parse(h);
          if (st != yatl_status_ok) { fails++; printf("FAIL max_depth_raised: status=%d\n", st); }
          yatl_free(h); }
        free(deep);
        /* lowered cap rejects a document deeper than it */
        total++;
        { yatl_handle h = yatl_alloc(&CB, NULL, NULL);
          yatl_status st;
          yatl_config(h, yatl_max_depth, (unsigned)2);
          st = yatl_parse(h, (const unsigned char *)"a:\n  b:\n    c: 1\n", 17);
          if (st == yatl_status_ok) st = yatl_complete_parse(h);
          if (st != yatl_status_error) { fails++; printf("FAIL max_depth_low: expected error\n"); }
          yatl_free(h); }
        /* a zero argument is rejected by yatl_config */
        total++;
        { yatl_handle h = yatl_alloc(&CB, NULL, NULL);
          if (yatl_config(h, yatl_max_depth, (unsigned)0) != 0) {
              fails++; printf("FAIL max_depth_zero: expected yatl_config to return 0\n"); }
          yatl_free(h); }
    }

    printf("\n%d/%d tests passed\n", total - fails, total);
    return fails ? 1 : 0;
}

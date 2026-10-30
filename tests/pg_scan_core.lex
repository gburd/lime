/*
** tests/pg_scan_core.lex -- PostgreSQL's SQL scanner, as plain lexing
** rules: the PG Lime port of scan.l (postgres/lime src/backend/parser/
** scan.lex: 12 states, 87 rules, the patterns copied from scan.l) with
** every action body replaced by PGSC(<rule>), which calls the shared,
** PostgreSQL-free action function in tests/pg_scan_core_act.h.  The
** patterns, states and rule order are byte-for-byte the port's.
**
** Used by tests/test_lex_program.c (B4) to compare the generated scanner
** (lime -X of this file) with the runtime DFA interpreter
** (lime_lex_program_compile of this file) on the same input: both run
** the same actions, so any difference is a matching difference.
**
** Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
** Portions Copyright (c) 1994, Regents of the University of California
** Used under the PostgreSQL License (see NOTICE).
*/


%name_prefix Core.

%include {
#include "pg_scan_core_act.h"
}

/* ----------------------------------------------------------------- */
/* Exclusive states                                                  */
/* ----------------------------------------------------------------- */

%exclusive_state XB.
%exclusive_state XC.
%exclusive_state XD.
%exclusive_state XH.
%exclusive_state XQ.
%exclusive_state XQS.
%exclusive_state XE.
%exclusive_state XDOLQ.
%exclusive_state XUI.
%exclusive_state XUS.
%exclusive_state XEU.

/* ----------------------------------------------------------------- */
/* Pattern fragments (from scan.l; identical regexes)                */
/* ----------------------------------------------------------------- */

%pattern space             /[ \t\n\r\f\v]/.
%pattern non_newline       /[^\n\r]/.
%pattern non_newline_space /[ \t\f\v]/.

%pattern comment   /--{non_newline}*/.

%pattern ws        /({space}+|{comment})/.
%pattern special_ws        /({space}+|{comment}\n|{comment}\r)/.
%pattern non_newline_ws    /({non_newline_space}|{comment})/.
%pattern ws_with_newline   /{non_newline_ws}*(\n|\r){special_ws}*/.

%pattern quote     /'/.
%pattern quotecontinue /{ws_with_newline}{quote}/.

%pattern xcstart /\/\*[\~\!\@\#\^\&\|\`\?\+\-\*\/\%\<\>\=]*/.
%pattern xcstop  /\*+\//.
%pattern xcinside /[^*\/]+/.

%pattern xbstart /[bB]'/.
%pattern xbinside /[^']+/.

%pattern xhstart /[xX]'/.
%pattern xhinside /[^']+/.

%pattern xnstart /[nN]'/.

%pattern xqstart /'/.
%pattern xqinside /[^'\\]+/.
%pattern xqdouble /''/.

%pattern xestart /[eE]'/.
%pattern xeinside /[^\\']+/.
%pattern xeunicode  /\\(u[0-9A-Fa-f]{4}|U[0-9A-Fa-f]{8})/.
%pattern xeunicodefail /\\(u[0-9A-Fa-f]{0,3}|U[0-9A-Fa-f]{0,7})/.
%pattern xeescape /\\[^0-7uUx]/.
%pattern xehexesc /\\x[0-9A-Fa-f]{1,2}/.
%pattern xehexfail /\\x/.
%pattern xeoctesc  /\\[0-7]{1,3}/.

%pattern xusstart /[uU]&'/.
%pattern xuistart /[uU]&"/.
%pattern xufailed /[uU]&/.

%pattern dolq_start /[A-Za-z_\x80-\xff]/.
%pattern dolq_cont  /[A-Za-z_0-9\x80-\xff]/.
%pattern dolqdelim  /\$({dolq_start}{dolq_cont}*)?\$/.
%pattern dolqfailed /\${dolq_start}{dolq_cont}*/.
%pattern dolqinside /[^$]+/.

%pattern xdstart /"/.
%pattern xdinside /[^"]+/.
%pattern xddouble /""/.

%pattern ident_start /[A-Za-z_\x80-\xff]/.
%pattern ident_cont  /[A-Za-z_0-9\$\x80-\xff]/.
%pattern identifier  /{ident_start}{ident_cont}*/.

%pattern self     /[,()\[\]\.;:|+\-*\/%^<>=]/.
%pattern op_chars /[\~\!\@\#\^\&\|\`\?\+\-\*\/\%\<\>\=]/.
%pattern operator /{op_chars}+/.

%pattern decdigit /[0-9]/.
%pattern hexdigit /[0-9A-Fa-f]/.
%pattern octdigit /[0-7]/.
%pattern bindigit /[01]/.

%pattern decinteger /{decdigit}(_?{decdigit})*/.
%pattern hexinteger /0[xX](_?{hexdigit})+/.
%pattern octinteger /0[oO](_?{octdigit})+/.
%pattern bininteger /0[bB](_?{bindigit})+/.

%pattern hexfail /0[xX]_?/.
%pattern octfail /0[oO]_?/.
%pattern binfail /0[bB]_?/.

%pattern numeric     /(({decinteger}\.{decinteger}?)|(\.{decinteger}))/.
%pattern numericfail /{decinteger}\.\./.

%pattern real     /({decinteger}|{numeric})[Ee][-+]?{decinteger}/.
%pattern realfail /({decinteger}|{numeric})[Ee][-+]/.

%pattern integer_junk /{decinteger}{identifier}/.
%pattern numeric_junk /{numeric}{identifier}/.
%pattern real_junk    /{real}{identifier}/.
%pattern param        /\${decdigit}+/.
%pattern param_junk   /\${decdigit}+{identifier}/.

/* =================================================================== */
/* INITIAL state rules (declaration order = tie-break order)           */
/* =================================================================== */

/* ----- Whitespace and SQL line comments ----- */
rule ws_initial matches /{ws}+/ { PGSC(CORE_RULE_WS_INITIAL); }

/* ----- C-style nested comment opener ----- */
rule xc_open matches /{xcstart}/ { PGSC(CORE_RULE_XC_OPEN); }

/* ----- xb' (bit string) ----- */
rule xb_open matches /{xbstart}/ { PGSC(CORE_RULE_XB_OPEN); }

/* ----- xh' (hex string) ----- */
rule xh_open matches /{xhstart}/ { PGSC(CORE_RULE_XH_OPEN); }

/* ----- xn' (national char): "n" plus pushed-back quote ----- */
rule xn_open matches /{xnstart}/ { PGSC(CORE_RULE_XN_OPEN); }

/* ----- xq' (single-quoted standard string) ----- */
rule xq_open matches /{xqstart}/ { PGSC(CORE_RULE_XQ_OPEN); }

/* ----- xe' (extended escape string) ----- */
rule xe_open matches /{xestart}/ { PGSC(CORE_RULE_XE_OPEN); }

/* ----- u&' (Unicode string) ----- */
rule xus_open matches /{xusstart}/ { PGSC(CORE_RULE_XUS_OPEN); }

/* ----- u&" (Unicode delimited identifier) ----- */
rule xui_open matches /{xuistart}/ { PGSC(CORE_RULE_XUI_OPEN); }

/* ----- u& with neither ' nor " : push back &, treat as ident "u" ----- */
rule xu_failed matches /{xufailed}/ { PGSC(CORE_RULE_XU_FAILED); }

/* ----- $foo$ dollar-quote opener ----- */
rule xdolq_open matches /{dolqdelim}/ { PGSC(CORE_RULE_XDOLQ_OPEN); }

/* ----- $foo without closing $ : emit '$', push back rest ----- */
rule xdolq_failed matches /{dolqfailed}/ { PGSC(CORE_RULE_XDOLQ_FAILED); }

/* ----- " (double-quote: open delimited identifier) ----- */
rule xd_open matches /{xdstart}/ { PGSC(CORE_RULE_XD_OPEN); }

/* ----- Multi-character punctuation (declared before {operator}) ----- */
rule typecast matches /::/ { PGSC(CORE_RULE_TYPECAST); }
rule dot_dot matches /\.\./ { PGSC(CORE_RULE_DOT_DOT); }
rule colon_eq matches /:=/ { PGSC(CORE_RULE_COLON_EQ); }

/* ----- Numeric literals -----
** Order from scan.l: decinteger, hex/oct/bin integer, *fail, numeric,
** numericfail, real, realfail, junks last.  Length-tie tiebreak by
** declaration order picks the proper integer/numeric over the catch-all
** {decinteger}{identifier} junk match.
*/
rule decinteger matches /{decinteger}/ { PGSC(CORE_RULE_DECINTEGER); }
rule hexinteger matches /{hexinteger}/ { PGSC(CORE_RULE_HEXINTEGER); }
rule octinteger matches /{octinteger}/ { PGSC(CORE_RULE_OCTINTEGER); }
rule bininteger matches /{bininteger}/ { PGSC(CORE_RULE_BININTEGER); }

rule hexfail matches /{hexfail}/ { PGSC(CORE_RULE_HEXFAIL); }
rule octfail matches /{octfail}/ { PGSC(CORE_RULE_OCTFAIL); }
rule binfail matches /{binfail}/ { PGSC(CORE_RULE_BINFAIL); }

rule numeric matches /{numeric}/ { PGSC(CORE_RULE_NUMERIC); }
rule numericfail matches /{numericfail}/ { PGSC(CORE_RULE_NUMERICFAIL); }

rule real matches /{real}/ { PGSC(CORE_RULE_REAL); }
rule realfail matches /{realfail}/ { PGSC(CORE_RULE_REALFAIL); }

rule integer_junk matches /{integer_junk}/ { PGSC(CORE_RULE_INTEGER_JUNK); }
rule numeric_junk matches /{numeric_junk}/ { PGSC(CORE_RULE_NUMERIC_JUNK); }
rule real_junk matches /{real_junk}/ { PGSC(CORE_RULE_REAL_JUNK); }

/* ----- $N parameter marker ----- */
rule param_junk matches /{param_junk}/ { PGSC(CORE_RULE_PARAM_JUNK); }
rule param matches /{param}/ { PGSC(CORE_RULE_PARAM); }

/* ----- Operator (driver trims comments + trailing +/-) ----- */
rule op matches /{operator}/ { PGSC(CORE_RULE_OP); }

/* ----- Identifier (driver does keyword lookup) ----- */
rule ident matches /{identifier}/ { PGSC(CORE_RULE_IDENT); }

/* ----- Single-char self/other (returned as raw ASCII byte) ----- */
rule self_ch matches /{self}/ { PGSC(CORE_RULE_SELF_CH); }
rule other matches /[\x00-\xff]/ { PGSC(CORE_RULE_OTHER); }

/* =================================================================== */
/* XC state: nested C-style comment                                    */
/* =================================================================== */

<XC> rule xc_nested matches /{xcstart}/ { PGSC(CORE_RULE_XC_NESTED); }
<XC> rule xc_close matches /{xcstop}/ { PGSC(CORE_RULE_XC_CLOSE); }
<XC> rule xc_inside matches /{xcinside}/ { PGSC(CORE_RULE_XC_INSIDE); }
<XC> rule xc_op_chars matches /{op_chars}+/ { PGSC(CORE_RULE_XC_OP_CHARS); }
<XC> rule xc_stars matches /\*+/ { PGSC(CORE_RULE_XC_STARS); }
<XC> rule xc_eof matches <<EOF>> { PGSC(CORE_RULE_XC_EOF); }

/* =================================================================== */
/* XB state: bit-string body                                           */
/* =================================================================== */

<XB> rule xb_inside matches /{xbinside}/ { PGSC(CORE_RULE_XB_INSIDE); }
<XB> rule xb_close matches /{quote}/ { PGSC(CORE_RULE_XB_CLOSE); }
<XB> rule xb_eof matches <<EOF>> { PGSC(CORE_RULE_XB_EOF); }

/* =================================================================== */
/* XH state: hex-string body                                           */
/* =================================================================== */

<XH> rule xh_inside matches /{xhinside}/ { PGSC(CORE_RULE_XH_INSIDE); }
<XH> rule xh_close matches /{quote}/ { PGSC(CORE_RULE_XH_CLOSE); }
<XH> rule xh_eof matches <<EOF>> { PGSC(CORE_RULE_XH_EOF); }

/* =================================================================== */
/* XQ state: standard quoted string body                               */
/* =================================================================== */

<XQ> rule xq_double matches /{xqdouble}/ { PGSC(CORE_RULE_XQ_DOUBLE); }
<XQ> rule xq_inside matches /[^']+/ { PGSC(CORE_RULE_XQ_INSIDE); }
<XQ> rule xq_close matches /{quote}/ { PGSC(CORE_RULE_XQ_CLOSE); }
<XQ> rule xq_eof matches <<EOF>> { PGSC(CORE_RULE_XQ_EOF); }

/* =================================================================== */
/* XE state: extended-escape quoted string body                        */
/* =================================================================== */

<XE> rule xe_double matches /{xqdouble}/ { PGSC(CORE_RULE_XE_DOUBLE); }
<XE> rule xe_unicode matches /{xeunicode}/ { PGSC(CORE_RULE_XE_UNICODE); }
<XE> rule xe_unicode_fail matches /{xeunicodefail}/ { PGSC(CORE_RULE_XE_UNICODE_FAIL); }
<XE> rule xe_escape matches /{xeescape}/ { PGSC(CORE_RULE_XE_ESCAPE); }
<XE> rule xe_hexesc matches /{xehexesc}/ { PGSC(CORE_RULE_XE_HEXESC); }
<XE> rule xe_hexfail matches /{xehexfail}/ { PGSC(CORE_RULE_XE_HEXFAIL); }
<XE> rule xe_octesc matches /{xeoctesc}/ { PGSC(CORE_RULE_XE_OCTESC); }
<XE> rule xe_inside matches /{xeinside}/ { PGSC(CORE_RULE_XE_INSIDE); }
<XE> rule xe_lone_backslash matches /\\/ { PGSC(CORE_RULE_XE_LONE_BACKSLASH); }
<XE> rule xe_close matches /{quote}/ { PGSC(CORE_RULE_XE_CLOSE); }
<XE> rule xe_eof matches <<EOF>> { PGSC(CORE_RULE_XE_EOF); }

/* =================================================================== */
/* XEU state: half-way through a Unicode surrogate pair                */
/* =================================================================== */

<XEU> rule xeu_unicode matches /{xeunicode}/ { PGSC(CORE_RULE_XEU_UNICODE); }
<XEU> rule xeu_unicode_fail matches /{xeunicodefail}/ { PGSC(CORE_RULE_XEU_UNICODE_FAIL); }
<XEU> rule xeu_other matches /[\x00-\xff]/ { PGSC(CORE_RULE_XEU_OTHER); }
<XEU> rule xeu_eof matches <<EOF>> { PGSC(CORE_RULE_XEU_EOF); }

/* =================================================================== */
/* XUS state: u&'...' Unicode-escape string body                       */
/* =================================================================== */

<XUS> rule xus_double matches /{xqdouble}/ { PGSC(CORE_RULE_XUS_DOUBLE); }
<XUS> rule xus_inside matches /[^']+/ { PGSC(CORE_RULE_XUS_INSIDE); }
<XUS> rule xus_close matches /{quote}/ { PGSC(CORE_RULE_XUS_CLOSE); }
<XUS> rule xus_eof matches <<EOF>> { PGSC(CORE_RULE_XUS_EOF); }

/* =================================================================== */
/* XQS state: quote-stop -- look for continuation                      */
/* =================================================================== */

<XQS> rule xqs_continue matches /{quotecontinue}/ { PGSC(CORE_RULE_XQS_CONTINUE); }
<XQS> rule xqs_other matches /[\x00-\xff]/ { PGSC(CORE_RULE_XQS_OTHER); }
<XQS> rule xqs_eof matches <<EOF>> { PGSC(CORE_RULE_XQS_EOF); }

/* =================================================================== */
/* XDOLQ state: dollar-quoted string body                              */
/* =================================================================== */

<XDOLQ> rule xdolq_close matches /{dolqdelim}/ { PGSC(CORE_RULE_XDOLQ_CLOSE); }
<XDOLQ> rule xdolq_inside matches /{dolqinside}/ { PGSC(CORE_RULE_XDOLQ_INSIDE); }
<XDOLQ> rule xdolq_inner_failed matches /{dolqfailed}/ { PGSC(CORE_RULE_XDOLQ_INNER_FAILED); }
<XDOLQ> rule xdolq_lone_dollar matches /\$/ { PGSC(CORE_RULE_XDOLQ_LONE_DOLLAR); }
<XDOLQ> rule xdolq_eof matches <<EOF>> { PGSC(CORE_RULE_XDOLQ_EOF); }

/* =================================================================== */
/* XD state: delimited identifier "..." body                           */
/* =================================================================== */

<XD> rule xd_double matches /{xddouble}/ { PGSC(CORE_RULE_XD_DOUBLE); }
<XD> rule xd_inside matches /{xdinside}/ { PGSC(CORE_RULE_XD_INSIDE); }
<XD> rule xd_close matches /{xdstart}/ { PGSC(CORE_RULE_XD_CLOSE); }
<XD> rule xd_eof matches <<EOF>> { PGSC(CORE_RULE_XD_EOF); }

/* =================================================================== */
/* XUI state: u&"..." delimited Unicode identifier body                */
/* =================================================================== */

<XUI> rule xui_double matches /{xddouble}/ { PGSC(CORE_RULE_XUI_DOUBLE); }
<XUI> rule xui_inside matches /{xdinside}/ { PGSC(CORE_RULE_XUI_INSIDE); }
<XUI> rule xui_close matches /{xdstart}/ { PGSC(CORE_RULE_XUI_CLOSE); }
<XUI> rule xui_eof matches <<EOF>> { PGSC(CORE_RULE_XUI_EOF); }

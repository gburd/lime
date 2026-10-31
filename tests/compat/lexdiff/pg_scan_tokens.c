/* Token-stream driver for PG's scan.l: core_yylex over each file, printing
** token, yylloc (byte offset), line number (counted from the offset), text.
** The backend symbols scan.l needs are stubbed; an ereport(ERROR) prints
** the message and moves on to the next statement file via longjmp. */
#include "postgres.h"
#include <setjmp.h>
#include "parser/scanner.h"
#include "common/keywords.h"
#include "mb/pg_wchar.h"
#include "parser/scansup.h"
#include "parser/parser.h"
#include "utils/elog.h"

ErrorContextCallback *error_context_stack = NULL;

static jmp_buf jb;
static int elevel_cur, errcode_cur;
void *palloc(Size n) { void *p = malloc(n ? n : 1); if (!p) abort(); return p; }
void *repalloc(void *p, Size n) { p = realloc(p, n ? n : 1); if (!p) abort(); return p; }
void pfree(void *p) { free(p); }
char *pstrdup(const char *s) { return strdup(s); }
bool errstart(int elevel, const char *domain) { (void) domain; elevel_cur = elevel; return true; }
bool errstart_cold(int elevel, const char *domain) { return errstart(elevel, domain); }
int errcode(int c) { errcode_cur = c; return 0; }
int errmsg(const char *fmt, ...) { printf("MSG\t%s\n", fmt); return 0; }
int errmsg_internal(const char *fmt, ...) { printf("MSGI\t%s\n", fmt); return 0; }
int errdetail(const char *fmt, ...) { (void) fmt; return 0; }
int errhint(const char *fmt, ...) { (void) fmt; return 0; }
int errposition(int p) { printf("POS\t%d\n", p); return 0; }
int geterrcode(void) { return errcode_cur; }
void errfinish(const char *f, int l, const char *fn) {
	(void) f; (void) l; (void) fn;
	printf("ELEVEL\t%d\n", elevel_cur);
	if (elevel_cur >= ERROR) longjmp(jb, 1);
}
int pg_get_client_encoding(void) { return PG_UTF8; }
int pg_mbstrlen_with_len(const char *s, int n) { int c = 0; for (int i = 0; i < n; i++) if (((unsigned char) s[i] & 0xC0) != 0x80) c++; return c; }
bool pg_verifymbstr(const char *s, int n, bool noError) { (void) s; (void) n; (void) noError; return true; }
void pg_unicode_to_server(pg_wchar c, unsigned char *s) {
	if (c < 0x80) { s[0] = c; s[1] = 0; }
	else if (c < 0x800) { s[0] = 0xC0 | (c >> 6); s[1] = 0x80 | (c & 0x3F); s[2] = 0; }
	else if (c < 0x10000) { s[0] = 0xE0 | (c >> 12); s[1] = 0x80 | ((c >> 6) & 0x3F); s[2] = 0x80 | (c & 0x3F); s[3] = 0; }
	else { s[0] = 0xF0 | (c >> 18); s[1] = 0x80 | ((c >> 12) & 0x3F); s[2] = 0x80 | ((c >> 6) & 0x3F); s[3] = 0x80 | (c & 0x3F); s[4] = 0; }
}
char *downcase_truncate_identifier(const char *ident, int len, bool warn) {
	(void) warn; char *r = malloc(len + 1);
	for (int i = 0; i < len; i++) { char c = ident[i]; r[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c; }
	r[len] = 0; if (len >= NAMEDATALEN) r[NAMEDATALEN - 1] = 0; return r;
}
void truncate_identifier(char *ident, int len, bool warn) { (void) warn; if (len >= NAMEDATALEN) ident[NAMEDATALEN - 1] = 0; }

static char *slurp(const char *path, long *n) {
	FILE *f = fopen(path, "rb"); if (!f) return NULL;
	fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
	char *b = malloc(*n + 1); if (fread(b, 1, *n, f) != (size_t) *n) { fclose(f); free(b); return NULL; }
	b[*n] = 0; fclose(f); return b;
}

int main(int argc, char **argv) {
	for (int i = 1; i < argc; i++) {
		long n; char *src = slurp(argv[i], &n);
		if (!src) continue;
		printf("FILE\t%s\n", argv[i]);
		/* one statement per scan, so an error only loses the rest of it */
		char *p = src;
		while (*p) {
			char *e = strchr(p, ';');
			e = e ? e + 1 : p + strlen(p);
			char save = *e; *e = 0;
			core_yy_extra_type ext; core_YYSTYPE v; YYLTYPE loc;
			volatile core_yyscan_t sc = scanner_init(p, &ext, &ScanKeywords, ScanKeywordTokens);
			if (setjmp(jb) == 0) {
				int t;
				while ((t = core_yylex(&v, &loc, sc)) != 0) {
					int line = 1; for (int k = 0; k < loc; k++) if (p[k] == '\n') line++;
					printf("%d\t%d\t%d\t", t, loc, line);
					/* gram.y numbers IDENT..Op 258..265 (<str>), ICONST/PARAM 266/267 (<ival>) */
					if (t >= 258 && t <= 265) printf("%s", v.str);
					else if (t == 266 || t == 267) printf("%d", v.ival);
					else if (t >= 268 && t <= 274) ;	/* operator tokens: no value */
					else if (t > 274) printf("%s", v.keyword);
					else if (t < 256) printf("%c", t);
					printf("\n");
				}
			} else
				printf("ERROR\n");
			*e = save; p = e;
		}
		free(src);
	}
	return 0;
}

/* numutils.c's contract for what scan.l feeds it ([0-9_] with an optional
** 0x/0o/0b prefix): the value, or error_occurred when it does not fit. */
#include "nodes/miscnodes.h"
int32 pg_strtoint32_safe(const char *s, Node *escontext) {
	char buf[256]; int j = 0, base = 10;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
	else if (s[0] == '0' && (s[1] == 'o' || s[1] == 'O')) { base = 8; s += 2; }
	else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) { base = 2; s += 2; }
	for (; *s && j < 255; s++) if (*s != '_') buf[j++] = *s;
	buf[j] = 0;
	errno = 0; char *end; long long v = strtoll(buf, &end, base);
	if (errno || *end || j == 0 || v > PG_INT32_MAX || v < PG_INT32_MIN) {
		((ErrorSaveContext *) escontext)->error_occurred = true; return 0;
	}
	return (int32) v;
}

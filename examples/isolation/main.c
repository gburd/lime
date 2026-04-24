/*-------------------------------------------------------------------------
 *
 * main.c
 *    Standalone driver for the isolation test spec parser.
 *
 * Usage:
 *   isol_parser file.spec        # parse and display
 *   isol_parser -q file.spec     # validate only
 *   isol_parser < file.spec      # read from stdin
 *
 *-------------------------------------------------------------------------
 */
#include "isolation_defs.h"
#include "isolation_gram.h"

/*
 * Lime parser interface (generated).
 */
void *isolAlloc(void *(*mallocProc)(size_t));
void  isolFree(void *p, void (*freeProc)(void *));
void  isol(void *yyp, int yymajor, IsolToken yyminor, IsolParseState *pstate);

static char *
read_file(FILE *fp, int *out_len)
{
    size_t capacity = 4096;
    size_t length = 0;
    char  *buf = malloc(capacity);

    if (!buf)
    {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    while (!feof(fp))
    {
        if (length + 1024 > capacity)
        {
            capacity *= 2;
            buf = realloc(buf, capacity);
            if (!buf)
            {
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
        }
        size_t n = fread(buf + length, 1, 1024, fp);
        length += n;
        if (n == 0) break;
    }

    buf[length] = '\0';
    *out_len = (int)length;
    return buf;
}

static int
parse_spec(const char *input, int length, int quiet)
{
    IsolParseState *pstate;
    void           *parser;
    IsolToken       token;
    int             token_type;
    int             result;

    pstate = isol_parse_state_create();
    isol_parse_state_set_input(pstate, input, length);

    parser = isolAlloc(malloc);
    if (!parser)
    {
        fprintf(stderr, "failed to allocate parser\n");
        isol_parse_state_destroy(pstate);
        return 1;
    }

    while ((token_type = isol_scan_next(pstate, &token)) != 0)
    {
        isol(parser, token_type, token, pstate);
        if (pstate->error_count > 0)
            break;
    }

    if (pstate->error_count == 0)
    {
        IsolToken eof_token = {0};
        isol(parser, 0, eof_token, pstate);
    }

    result = pstate->error_count;

    if (result == 0 && !quiet)
        isol_testspec_print(&pstate->result, stdout);

    if (result == 0)
        fprintf(stderr, "Parse successful.\n");
    else
        fprintf(stderr, "Parse failed with %d error(s).\n", result);

    isolFree(parser, free);
    isol_parse_state_destroy(pstate);

    return result;
}

int
main(int argc, char *argv[])
{
    FILE *input_file = stdin;
    int   quiet = 0;
    int   argidx = 1;
    char *input;
    int   length;
    int   result;

    while (argidx < argc && argv[argidx][0] == '-')
    {
        if (strcmp(argv[argidx], "-q") == 0)
        {
            quiet = 1;
            argidx++;
        }
        else if (strcmp(argv[argidx], "-h") == 0 ||
                 strcmp(argv[argidx], "--help") == 0)
        {
            fprintf(stderr, "Usage: %s [-q] [file.spec]\n", argv[0]);
            fprintf(stderr, "  -q  Quiet mode (validate only)\n");
            return 0;
        }
        else
        {
            fprintf(stderr, "Unknown option: %s\n", argv[argidx]);
            return 1;
        }
    }

    if (argidx < argc)
    {
        input_file = fopen(argv[argidx], "r");
        if (!input_file)
        {
            fprintf(stderr, "Cannot open '%s': %s\n",
                    argv[argidx], strerror(errno));
            return 1;
        }
    }

    input = read_file(input_file, &length);
    if (input_file != stdin)
        fclose(input_file);

    result = parse_spec(input, length, quiet);
    free(input);

    return result ? 1 : 0;
}

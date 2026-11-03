/*
 * lsp_format.c -- textDocument/formatting handler.
 *
 * Formats the document in process with lime_format_grammar_in_process()
 * (liblime_compiler), which returns exactly the bytes `lime -F` writes to
 * <file>.formatted, and answers with one TextEdit replacing the whole
 * document.  No temp file, no child process.
 */
#include "lsp_format.h"
#include "lsp_json.h"
#include "lime_compiler.h"

#include <stdlib.h>
#include <string.h>

/* Compute the LSP `position` (line/character) at the END of `text`.
 * LSP positions are zero-indexed line + UTF-16 character offset
 * within the line.  Lime grammars are ASCII-or-UTF-8; we approximate
 * UTF-16 char offset by the count of bytes since the last newline,
 * which is exact for ASCII and correct enough for UTF-8 in identifier
 * positions (BMP chars are 1 code unit each).  Surrogate-pair
 * grammars don't exist in practice. */
static void end_position(const char *text, size_t text_len,
                         long long *out_line, long long *out_char) {
    long long line = 0;
    long long col  = 0;
    for (size_t i = 0; i < text_len; i++) {
        if (text[i] == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
    }
    *out_line = line;
    *out_char = col;
}

json_value *lsp_format_run(const char *text, size_t text_len) {
    json_value *result = json_make_array();
    if (result == NULL || text == NULL || text_len == 0) return result;

    char *formatted = NULL;
    /* A grammar that does not parse is left alone: an empty edit array is
     * the LSP way to say "no change"; diagnostics report the error. */
    if (lime_format_grammar_in_process(text, text_len, &formatted, NULL) != 0
        || formatted == NULL) {
        free(formatted);
        return result;
    }

    /* One TextEdit replacing 0:0 -> end with the formatted text. */
    long long end_line = 0, end_char = 0;
    end_position(text, text_len, &end_line, &end_char);

    json_value *edit    = json_make_object();
    json_value *range   = json_make_object();
    json_value *start   = json_make_object();
    json_value *end_pos = json_make_object();
    json_object_set(start, "line",      json_make_int(0));
    json_object_set(start, "character", json_make_int(0));
    json_object_set(end_pos, "line",      json_make_int(end_line));
    json_object_set(end_pos, "character", json_make_int(end_char));
    json_object_set(range, "start", start);
    json_object_set(range, "end",   end_pos);
    json_object_set(edit, "range", range);
    json_object_set(edit, "newText",
                    json_make_string_n(formatted, strlen(formatted)));
    json_array_push(result, edit);
    free(formatted);
    return result;
}

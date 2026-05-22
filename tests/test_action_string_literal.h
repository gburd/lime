/*
** Header shared between the grammar action body and the driver.
** Defines the capture struct the action stashes its literal
** strings, char literals, and post-comment markers into.
*/
#ifndef TEST_ACTION_STRING_LITERAL_H
#define TEST_ACTION_STRING_LITERAL_H

struct pasl_capture {
    const char *s_plain;
    const char *s_with_double_slash;
    const char *s_with_block_open;
    const char *s_with_escaped_quote;
    const char *s_adjacent;
    char        c_a;
    char        c_n;
    char        c_apos;
    int         c_after_block;
    int         c_after_line;
};

#endif

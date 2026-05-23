/*
** cobol_state.h -- shared parser state across the Lime-generated
** parser (.c emits an %include of this) and the host driver (main.c).
**
** Kept as a tiny header so main.c and the grammar's reduce actions
** see the same struct definition.  Anything you'd want to thread
** through %extra_argument lives here.
*/

#ifndef COBOL_STATE_H
#define COBOL_STATE_H

struct cobol_parse_state {
    int errors;        /* syntax errors seen */
    int programs;      /* PROGRAM-IDs */
    int paragraphs;    /* paragraphs */
    int statements;    /* procedural statements */
    int data_items;    /* data-division entries */
};

static inline void cobol_count_program(struct cobol_parse_state *s) {
    if (s) s->programs++;
}
static inline void cobol_count_paragraph(struct cobol_parse_state *s) {
    if (s) s->paragraphs++;
}
static inline void cobol_count_stmt(struct cobol_parse_state *s) {
    if (s) s->statements++;
}
static inline void cobol_count_data(struct cobol_parse_state *s) {
    if (s) s->data_items++;
}

#endif /* COBOL_STATE_H */

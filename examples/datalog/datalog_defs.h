/*
 * datalog_defs.h
 *    Type definitions for the Datalog/EDN parser.
 *
 * Defines the AST node types, token carrier, and parse state
 * used by the Lime-generated parser and helper functions.
 */

#ifndef DATALOG_DEFS_H
#define DATALOG_DEFS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * Token carrier
 * ====================================================================== */

/*
 * Every token carries a string value (the lexeme text) plus its length.
 * The parser generator uses this as %token_type.
 */
typedef struct DatalogString {
    const char *val;
    int         len;
} DatalogString;

typedef struct DatalogToken {
    DatalogString str;
} DatalogToken;

/* ======================================================================
 * AST node types
 * ====================================================================== */

/* Term kinds -- what a term node represents */
typedef enum DlTermKind {
    DL_TERM_VARIABLE,       /* logic variable: X, Y, Parent */
    DL_TERM_ANON_VAR,       /* anonymous variable: _ */
    DL_TERM_ATOM,           /* symbolic atom: tom, bob */
    DL_TERM_INTEGER,        /* integer constant: 42, -7 */
    DL_TERM_FLOAT,          /* float constant: 3.14 */
    DL_TERM_STRING,         /* string constant: "hello" */
    DL_TERM_BOOLEAN,        /* true or false */
    DL_TERM_NIL,            /* nil value */
    DL_TERM_KEYWORD,        /* EDN keyword: :name, :person/age */
    DL_TERM_VECTOR,         /* EDN vector: [1, 2, 3] */
    DL_TERM_MAP,            /* EDN map: {:a 1, :b 2} */
    DL_TERM_SET,            /* EDN set: #{1, 2, 3} */
} DlTermKind;

/* Forward declarations */
typedef struct DlTerm DlTerm;
typedef struct DlTermList DlTermList;
typedef struct DlLiteral DlLiteral;
typedef struct DlLiteralList DlLiteralList;
typedef struct DlClause DlClause;
typedef struct DlClauseList DlClauseList;
typedef struct DlProgram DlProgram;

/* A single term (argument to a predicate) */
struct DlTerm {
    DlTermKind  kind;
    union {
        DatalogString   name;       /* variable, atom, keyword names */
        DatalogString   str_val;    /* string literal value */
        DatalogString   num_str;    /* integer/float as string */
        bool            bool_val;   /* boolean value */
        DlTermList     *elements;   /* vector, set elements; map pairs */
    } u;
};

/* Linked list of terms */
typedef struct DlTermCell {
    DlTerm             *data;
    struct DlTermCell  *next;
} DlTermCell;

struct DlTermList {
    int          length;
    DlTermCell  *head;
    DlTermCell  *tail;
};

/* Comparison operators */
typedef enum DlCompOp {
    DL_COMP_EQ,     /* = */
    DL_COMP_NEQ,    /* \= or != */
    DL_COMP_LT,     /* < */
    DL_COMP_GT,     /* > */
    DL_COMP_LTE,    /* <= */
    DL_COMP_GTE,    /* >= */
} DlCompOp;

/* Aggregation functions */
typedef enum DlAggFunc {
    DL_AGG_COUNT,
    DL_AGG_SUM,
    DL_AGG_MIN,
    DL_AGG_MAX,
    DL_AGG_AVG,
} DlAggFunc;

/* Literal kinds */
typedef enum DlLiteralKind {
    DL_LIT_POSITIVE,    /* normal positive literal */
    DL_LIT_NEGATED,     /* negation-as-failure */
    DL_LIT_COMPARISON,  /* built-in comparison */
    DL_LIT_AGGREGATE,   /* aggregation expression */
} DlLiteralKind;

/* A literal (predicate application) */
struct DlLiteral {
    DlLiteralKind  kind;
    DatalogString  predicate;   /* predicate name (for positive/negated) */
    DlTermList    *args;        /* arguments */
    union {
        struct {                /* for DL_LIT_COMPARISON */
            DlCompOp op;
            DlTerm  *left;
            DlTerm  *right;
        } comp;
        struct {                /* for DL_LIT_AGGREGATE */
            DlAggFunc  func;
            DlTerm    *target;
            DlLiteral *body;
        } agg;
        DlLiteral *inner;      /* for DL_LIT_NEGATED: the negated literal */
    } u;
};

/* Linked list of literals (rule body) */
typedef struct DlLiteralCell {
    DlLiteral             *data;
    struct DlLiteralCell  *next;
} DlLiteralCell;

struct DlLiteralList {
    int             length;
    DlLiteralCell  *head;
    DlLiteralCell  *tail;
};

/* Clause kinds */
typedef enum DlClauseKind {
    DL_CLAUSE_FACT,     /* head. */
    DL_CLAUSE_RULE,     /* head :- body. */
    DL_CLAUSE_QUERY,    /* ?- body. */
} DlClauseKind;

/* A clause (fact, rule, or query) */
struct DlClause {
    DlClauseKind    kind;
    DlLiteral      *head;  /* NULL for queries */
    DlLiteralList  *body;  /* NULL for facts */
};

/* Linked list of clauses */
typedef struct DlClauseCell {
    DlClause             *data;
    struct DlClauseCell  *next;
} DlClauseCell;

struct DlClauseList {
    int             length;
    DlClauseCell   *head;
    DlClauseCell   *tail;
};

/* Top-level program node */
struct DlProgram {
    DlClauseList *clauses;
    int           nfacts;
    int           nrules;
    int           nqueries;
};

/* ======================================================================
 * Parse state
 * ====================================================================== */

typedef struct DatalogParseState {
    DlProgram  *result;     /* parse result */
    int         error;      /* non-zero on error */
    const char *errMsg;     /* error message (if any) */
    int         anon_count; /* counter for anonymous variable ids */
} DatalogParseState;

#ifdef __cplusplus
}
#endif

#endif /* DATALOG_DEFS_H */

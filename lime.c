/*
** This file contains all sources (including headers) to the LEMON
** LALR(1) parser generator.  The sources have been combined into a
** single file to make it easy to include LEMON in the source tree
** and Makefile of another program.
**
** The author of this program disclaims copyright.
*/
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <stdint.h>
#include <limits.h>

/*
** Stub for the .lex compiler entry point.  The real implementation
** lives in src/lex/lex_main.c (compiled into lime_lex_compiler_lib).
** When meson builds lime it sets -DLIME_HAS_LEX_COMPILER and links
** the library, providing the real definition.  Without that flag
** (e.g. the single-file `cc -o lime lime.c` build) this stub keeps
** the link clean and `-X` reports a clear error explaining how to
** enable the lex frontend.
*/
#ifndef LIME_HAS_LEX_COMPILER
int lime_lex_run_compiler(const char *input_path, const char *output_dir) {
    (void)input_path; (void)output_dir;
    fprintf(stderr,
      "lime: -X (.lex compiler) not available in this build.\n"
      "      Rebuild with the lex compiler library:\n"
      "        meson setup builddir && ninja -C builddir lime\n"
      "      or compile lime together with the lex sources:\n"
      "        cc -o lime lime.c src/lex/*.c -Iinclude -Isrc/lex \\\n"
      "           -DLIME_HAS_LEX_COMPILER\n");
    return 1;
}
#endif

#define ISSPACE(X) isspace((unsigned char)(X))
#define ISDIGIT(X) isdigit((unsigned char)(X))
#define ISALNUM(X) isalnum((unsigned char)(X))
#define ISALPHA(X) isalpha((unsigned char)(X))
#define ISUPPER(X) isupper((unsigned char)(X))
#define ISLOWER(X) islower((unsigned char)(X))

/*
** Lime version string.  Must be kept in sync with the project()
** version in meson.build.  Reported by `lime -x` and `lime -v`, and
** mirrored by lime_parser_version() in src/version.c.
*/
#ifndef LIME_VERSION_STRING
#define LIME_VERSION_STRING "0.4.2"
#endif


#ifndef __WIN32__
#   if defined(_WIN32) || defined(WIN32)
#       define __WIN32__
#   endif
#endif

#ifdef __WIN32__
#ifdef __cplusplus
extern "C" {
#endif
extern int access(const char *path, int mode);
#ifdef __cplusplus
}
#endif
#else
#if defined(_WIN32)
#include <io.h>
#define dup2 _dup2
#define close _close
#define open _open
#else
#include <unistd.h>
#endif
#endif

/* #define PRIVATE static */
#define PRIVATE

#ifdef TEST
#define MAXRHS 5       /* Set low to exercise exception code */
#else
#define MAXRHS 1000
#endif

extern void memory_error();
static int showPrecedenceConflict = 0;
static char *msort(char*,char**,int(*)(const char*,const char*));

/*
** Compilers are getting increasingly pedantic about type conversions
** as C evolves ever closer to Ada....  To work around the latest problems
** we have to define the following variant of strlen().
*/
#define lemonStrlen(X)   ((int)strlen(X))

/*
** Header on the linked list of memory allocations.
*/
typedef struct MemChunk MemChunk;
struct MemChunk {
  MemChunk *pNext;
  size_t sz;
  /* Actually memory follows */
};  

/*
** Global linked list of all memory allocations.
*/
static MemChunk *memChunkList = 0;

/*
** Wrappers around malloc(), calloc(), realloc() and free().
**
** All memory allocations are kept on a doubly-linked list.  The
** lime_free_all() function can be called prior to exit to clean
** up any memory leaks.
**
** This is not necessary.  But compilers and getting increasingly
** fussy about memory leaks, even in command-line programs like Lemon
** where they do not matter.  So this code is provided to hush the
** warnings.
*/
static void *lime_malloc(size_t nByte){
  MemChunk *p;
  p = malloc( nByte + sizeof(MemChunk) );
  if( p==0 ){
    fprintf(stderr, "Out of memory.  Failed to allocate %lld bytes.\n",
            (long long int)nByte);
    exit(1);
  }
  p->pNext = memChunkList;
  p->sz = nByte;
  memChunkList = p;
  return (void*)&p[1];
}
static void *lime_calloc(size_t nElem, size_t sz){
  void *p = lime_malloc(nElem*sz);
  memset(p, 0, nElem*sz);
  return p;
}
static void lime_free(void *pOld){
  if( pOld ){
    MemChunk *p = (MemChunk*)pOld;
    p--;
    memset(pOld, 0, p->sz);
  }
}
static void *lime_realloc(void *pOld, size_t nNew){
  void *pNew;
  MemChunk *p;
  if( pOld==0 ) return lime_malloc(nNew);
  p = (MemChunk*)pOld;
  p--;
  if( p->sz>=nNew ) return pOld;
  pNew = lime_malloc( nNew );
  memcpy(pNew, pOld, p->sz);
  return pNew;
}

/* Free all outstanding memory allocations.
** Do this right before exiting.
*/
static void lime_free_all(void){
  while( memChunkList ){
    MemChunk *pNext = memChunkList->pNext;
    free( memChunkList );
    memChunkList = pNext;
  }
}

/*
** Compilers are starting to complain about the use of sprintf() and strcpy(),
** saying they are unsafe.  So we define our own versions of those routines too.
**
** There are three routines here:  lemon_sprintf(), lemon_vsprintf(), and
** lemon_addtext(). The first two are replacements for sprintf() and vsprintf().
** The third is a helper routine for vsnprintf() that adds texts to the end of a
** buffer, making sure the buffer is always zero-terminated.
**
** The string formatter is a minimal subset of stdlib sprintf() supporting only
** a few simply conversions:
**
**   %d
**   %s
**   %.*s
**
*/
static void lemon_addtext(
  char *zBuf,           /* The buffer to which text is added */
  int *pnUsed,          /* Slots of the buffer used so far */
  const char *zIn,      /* Text to add */
  int nIn,              /* Bytes of text to add.  -1 to use strlen() */
  int iWidth            /* Field width.  Negative to left justify */
){
  if( nIn<0 ) for(nIn=0; zIn[nIn]; nIn++){}
  while( iWidth>nIn ){ zBuf[(*pnUsed)++] = ' '; iWidth--; }
  if( nIn==0 ) return;
  memcpy(&zBuf[*pnUsed], zIn, nIn);
  *pnUsed += nIn;
  while( (-iWidth)>nIn ){ zBuf[(*pnUsed)++] = ' '; iWidth++; }
  zBuf[*pnUsed] = 0;
}
static int lemon_vsprintf(char *str, const char *zFormat, va_list ap){
  int i, j, k, c;
  int nUsed = 0;
  const char *z;
  char zTemp[50];
  str[0] = 0;
  for(i=j=0; (c = zFormat[i])!=0; i++){
    if( c=='%' ){
      int iWidth = 0;
      lemon_addtext(str, &nUsed, &zFormat[j], i-j, 0);
      c = zFormat[++i];
      if( ISDIGIT(c) || (c=='-' && ISDIGIT(zFormat[i+1])) ){
        if( c=='-' ) i++;
        while( ISDIGIT(zFormat[i]) ) iWidth = iWidth*10 + zFormat[i++] - '0';
        if( c=='-' ) iWidth = -iWidth;
        c = zFormat[i];
      }
      if( c=='d' ){
        int v = va_arg(ap, int);
        if( v<0 ){
          lemon_addtext(str, &nUsed, "-", 1, iWidth);
          v = -v;
        }else if( v==0 ){
          lemon_addtext(str, &nUsed, "0", 1, iWidth);
        }
        k = 0;
        while( v>0 ){
          k++;
          zTemp[sizeof(zTemp)-k] = (v%10) + '0';
          v /= 10;
        }
        lemon_addtext(str, &nUsed, &zTemp[sizeof(zTemp)-k], k, iWidth);
      }else if( c=='s' ){
        z = va_arg(ap, const char*);
        lemon_addtext(str, &nUsed, z, -1, iWidth);
      }else if( c=='.' && memcmp(&zFormat[i], ".*s", 3)==0 ){
        i += 2;
        k = va_arg(ap, int);
        z = va_arg(ap, const char*);
        lemon_addtext(str, &nUsed, z, k, iWidth);
      }else if( c=='%' ){
        lemon_addtext(str, &nUsed, "%", 1, 0);
      }else{
        fprintf(stderr, "illegal format\n");
        exit(1);
      }
      j = i+1;
    }
  }
  lemon_addtext(str, &nUsed, &zFormat[j], i-j, 0);
  return nUsed;
}
static int lemon_sprintf(char *str, const char *format, ...){
  va_list ap;
  int rc;
  va_start(ap, format);
  rc = lemon_vsprintf(str, format, ap);
  va_end(ap);
  return rc;
}
static void lemon_strcpy(char *dest, const char *src){
  while( (*(dest++) = *(src++))!=0 ){}
}
static void lemon_strcat(char *dest, const char *src){
  while( *dest ) dest++;
  lemon_strcpy(dest, src);
}


/* a few forward declarations... */
struct rule;
struct lime;
struct action;
struct symbol;

/* Lime-Letter-21: per-token / per-type group records.
** The formatter (`lime -F`) emits %token / %type sections in
** declaration order grouped into runs of contiguous same-kind
** directives.  Each group carries an optional leading comment
** (the comment block immediately above the run's first directive)
** plus the symbols collected in source order across the run.
** Inter-symbol comments INSIDE a run are intentionally dropped --
** PG-team-accepted cheap variant: section banners between groups
** capture ~90 % of the lossage at a fraction of the implementation
** cost of per-individual-symbol comments. */
typedef struct LimeTokenGroup LimeTokenGroup;
struct LimeTokenGroup {
  char            *leading_comment;   /* heap; NULL if no leading comment */
  struct symbol  **symbols;           /* heap; symbols in this group */
  int              n_symbols;
  int              cap_symbols;
  LimeTokenGroup  *next;              /* declaration-order singly-linked list */
};
typedef struct LimeTypeGroup LimeTypeGroup;
struct LimeTypeGroup {
  char            *leading_comment;
  struct symbol  **symbols;
  int              n_symbols;
  int              cap_symbols;
  LimeTypeGroup   *next;
};

static struct action *Action_new(void);
static struct action *Action_sort(struct action *);

/********** From the file "build.h" ************************************/
void FindRulePrecedences(struct lime*);
void FindFirstSets(struct lime*);
void FindStates(struct lime*);
void FindLinks(struct lime*);
void FindFollowSets(struct lime*);
void FindActions(struct lime*);

/********* From the file "configlist.h" *********************************/
void Configlist_init(void);
struct config *Configlist_add(struct rule *, int);
struct config *Configlist_addbasis(struct rule *, int);
void Configlist_closure(struct lime *);
void Configlist_sort(void);
void Configlist_sortbasis(void);
struct config *Configlist_return(void);
struct config *Configlist_basis(void);
void Configlist_eat(struct config *);
void Configlist_reset(void);

/********* From the file "error.h" ***************************************/
void ErrorMsg(const char *, int,const char *, ...);

/****** From the file "option.h" ******************************************/
enum option_type { OPT_FLAG=1,  OPT_INT,  OPT_DBL,  OPT_STR,
         OPT_FFLAG, OPT_FINT, OPT_FDBL, OPT_FSTR};
struct s_options {
  enum option_type type;
  const char *label;
  char *arg;
  const char *message;
};
int    OptInit(char**,struct s_options*,FILE*);
int    OptNArgs(void);
char  *OptArg(int);
void   OptErr(int);
void   OptPrint(void);

/******** From the file "parse.h" *****************************************/
void Parse(struct lime *lemp);

/* v0.4.1 forward decls (%extends + diamond machinery).  The full
** definitions live near the bottom of the file alongside Parse(),
** but commit_current_rule() (in the parser body, much earlier)
** needs them to be visible. */
struct pstate;
static struct rule *find_rule_by_identity(struct pstate *psp,
                                          struct rule *probe);
static const char *resolve_extends_path(const char *current_file,
                                        const char *target);
static void parse_lime_file_recursive(struct pstate *psp,
                                      const char *filename,
                                      int is_top_level);
static void unlink_and_free_rule(struct pstate *psp, struct rule *target);

/********* From the file "plink.h" ***************************************/
struct plink *Plink_new(void);
void Plink_add(struct plink **, struct config *);
void Plink_copy(struct plink **, struct plink *);
void Plink_delete(struct plink *);

/********** From the file "report.h" *************************************/
void Reprint(struct lime *);
void ReportOutput(struct lime *);
void ReportTable(struct lime *, int, int);
void ReportAOTTable(struct lime *);
void ReportSnapshotInit(struct lime *);
void ReportHeader(struct lime *);
static void GenerateASTActions(struct lime *);
void CompressTables(struct lime *);
void ResortStates(struct lime *);

/********** From the file "set.h" ****************************************/
void  SetSize(int);             /* All sets will be of size N */
char *SetNew(void);               /* A new set for element 0..N */
void  SetFree(char*);             /* Deallocate a set */
int SetAdd(char*,int);            /* Add element to a set */
int SetUnion(char *,char *);    /* A <- A U B, thru element N */
#define SetFind(X,Y) (X[Y])       /* True if Y is in set X */

/********** From the file "struct.h" *************************************/
/*
** Principal data structures for the LEMON parser generator.
*/

typedef enum {LEMON_FALSE=0, LEMON_TRUE} Boolean;

/* Symbols (terminals and nonterminals) of the grammar are stored
** in the following: */
enum symbol_type {
  TERMINAL,
  NONTERMINAL,
  MULTITERMINAL
};
enum e_assoc {
    LEFT,
    RIGHT,
    NONE,
    UNK
};
struct symbol {
  const char *name;        /* Name of the symbol */
  int index;               /* Index number for this symbol */
  enum symbol_type type;   /* Symbols are all either TERMINALS or NTs */
  struct rule *rule;       /* Linked list of rules of this (if an NT) */
  struct symbol *fallback; /* fallback token in case this token doesn't parse */
  int prec;                /* Precedence if defined (-1 otherwise) */
  enum e_assoc assoc;      /* Associativity if precedence is defined */
  char *firstset;          /* First-set for all rules of this symbol */
  Boolean lambda;          /* True if NT and can generate an empty string */
  int useCnt;              /* Number of times used */
  char *destructor;        /* Code which executes whenever this symbol is
                           ** popped from the stack during error processing */
  int destLineno;          /* Line number for start of destructor.  Set to
                           ** -1 for duplicate destructors. */
  char *datatype;          /* The data type of information held by this
                           ** object. Only used if type==NONTERMINAL */
  int dtnum;               /* The data type number.  In the parser, the value
                           ** stack is a union.  The .yy%d element of this
                           ** union is the correct data type for this object */
  int bContent;            /* True if this symbol ever carries content - if
                           ** it is ever more than just syntax */
  /* The following fields are used by MULTITERMINALs only */
  int nsubsym;             /* Number of constituent symbols in the MULTI */
  struct symbol **subsym;  /* Array of constituent symbols */
};

/* Each production rule in the grammar is stored in the following
** structure.  */
struct rule {
  struct symbol *lhs;      /* Left-hand side of the rule */
  const char *lhsalias;    /* Alias for the LHS (NULL if none) */
  int lhsStart;            /* True if left-hand side is the start symbol */
  int ruleline;            /* Line number for the rule */
  int nrhs;                /* Number of RHS symbols */
  struct symbol **rhs;     /* The RHS symbols */
  const char **rhsalias;   /* An alias for each RHS symbol (NULL if none) */
  int line;                /* Line number at which code begins */
  const char *code;        /* The code executed when this rule is reduced */
  const char *codePrefix;  /* Setup code before code[] above */
  const char *codeSuffix;  /* Breakdown code after code[] above */
  struct symbol *precsym;  /* Precedence symbol for this rule */
  int index;               /* An index number for this rule */
  int iRule;               /* Rule number as used in the generated tables */
  Boolean noCode;          /* True if this rule has no associated C code */
  Boolean codeEmitted;     /* True if the code has been emitted already */
  Boolean canReduce;       /* True if this rule is ever reduced */
  Boolean doesReduce;      /* Reduce actions occur after optimization */
  Boolean neverReduce;     /* Reduce is theoretically possible, but prevented
                           ** by actions or other outside implementation */
  /* Lime-Letter-19: heap-alloc'd inter-rule comment block captured
  ** by the tokenizer during Parse().  Emitted verbatim by `lime -F`
  ** immediately before the rule's `lhs ::= ...` line.  NULL when
  ** the rule has no leading comment (or, for non-first alternatives
  ** in a `|`-alternation group, the comment was attached to the
  ** first rule of the group). */
  char *leading_comment;
  /* v0.4.1 (%extends): provenance for diamond-inheritance and
  ** %override / %remove identity matching.  origin_file is the
  ** Strsafe-interned absolute (or as-resolved) path of the
  ** .lime file the rule was parsed from.  origin_line is the
  ** line of the rule's LHS within that file.  Both NULL/0 only
  ** if commit_current_rule is called outside of any file (never
  ** in practice). */
  const char *origin_file;
  int   origin_line;
  /* v0.4.1: %override bookkeeping.  override_depth is INT_MAX for
  ** rules that have never been the target of a %override (came in
  ** as plain rules from a base or derived file).  When a %override
  ** matches a rule, the matched rule's body is replaced and
  ** override_depth is set to the extends_depth of the file that
  ** issued the %override.  Diamond resolution: a later %override
  ** at SHALLOWER depth (closer to the user-invoked file) wins
  ** unconditionally; same depth from a different origin_file is a
  ** conflict; deeper depth is silently skipped (the more-derived
  ** decision already made on this rule stands). */
  int   override_depth;
  /* v0.4.1: 1 if this rule has ever been the target of a
  ** %override directive.  Distinct from override_depth being
  ** finite because we want a clean boolean for the diamond
  ** decision tree ("is this rule's body owned by a derived
  ** file or is it still verbatim from the base?"). */
  int   is_overridden;
  /* v0.4.1: 1 if a %remove or conflicting same-depth %override hit
  ** this rule and the conflict has not yet been resolved by a
  ** shallower %override.  Swept at end of Parse(); any rule still
  ** carrying conflict_pending=1 produces a hard error. */
  int   conflict_pending;
  /* v0.4.1: file (Strsafe'd) and depth that issued the most
  ** recent override.  Used for diagnostic strings on diamond
  ** conflicts so the user sees BOTH paths' filenames. */
  const char *override_file;
  /* v0.4.1: when conflict_pending=1, the file that contributed
  ** the conflicting override / remove.  NULL otherwise. */
  const char *conflict_file;
  int conflict_line;
  struct rule *nextlhs;    /* Next rule with the same LHS */
  struct rule *next;       /* Next rule in the global list */
};

/* A configuration is a production rule of the grammar together with
** a mark (dot) showing how much of that rule has been processed so far.
** Configurations also contain a follow-set which is a list of terminal
** symbols which are allowed to immediately follow the end of the rule.
** Every configuration is recorded as an instance of the following: */
enum cfgstatus {
  COMPLETE,
  INCOMPLETE
};
struct config {
  struct rule *rp;         /* The rule upon which the configuration is based */
  int dot;                 /* The parse point */
  char *fws;               /* Follow-set for this configuration only */
  struct plink *fplp;      /* Follow-set forward propagation links */
  struct plink *bplp;      /* Follow-set backwards propagation links */
  struct state *stp;       /* Pointer to state which contains this */
  enum cfgstatus status;   /* used during followset and shift computations */
  struct config *next;     /* Next configuration in the state */
  struct config *bp;       /* The next basis configuration */
};

enum e_action {
  SHIFT,
  ACCEPT,
  REDUCE,
  ERROR,
  SSCONFLICT,              /* A shift/shift conflict */
  SRCONFLICT,              /* Was a reduce, but part of a conflict */
  RRCONFLICT,              /* Was a reduce, but part of a conflict */
  SH_RESOLVED,             /* Was a shift.  Precedence resolved conflict */
  RD_RESOLVED,             /* Was reduce.  Precedence resolved conflict */
  NOT_USED,                /* Deleted by compression */
  SHIFTREDUCE              /* Shift first, then reduce */
};

/* Every shift or reduce operation is stored as one of the following */
struct action {
  struct symbol *sp;       /* The look-ahead symbol */
  enum e_action type;
  union {
    struct state *stp;     /* The new state, if a shift */
    struct rule *rp;       /* The rule, if a reduce */
  } x;
  struct symbol *spOpt;    /* SHIFTREDUCE optimization to this symbol */
  struct action *next;     /* Next action for this state */
  struct action *collide;  /* Next action with the same hash */
};

/* Each state of the generated parser's finite state machine
** is encoded as an instance of the following structure. */
struct state {
  struct config *bp;       /* The basis configurations for this state */
  struct config *cfp;      /* All configurations in this set */
  int statenum;            /* Sequential number for this state */
  struct action *ap;       /* List of actions for this state */
  int nTknAct, nNtAct;     /* Number of actions on terminals and nonterminals */
  int iTknOfst, iNtOfst;   /* yy_action[] offset for terminals and nonterms */
  int iDfltReduce;         /* Default action is to REDUCE by this rule */
  struct rule *pDfltReduce;/* The default REDUCE rule. */
  int autoReduce;          /* True if this is an auto-reduce state */
};
#define NO_OFFSET (-2147483647)

/* A followset propagation link indicates that the contents of one
** configuration followset should be propagated to another whenever
** the first changes. */
struct plink {
  struct config *cfp;      /* The configuration to which linked */
  struct plink *next;      /* The next propagate link */
};

/* Module composition metadata structures */
struct module_dependency {
  char *name;                  /* Module name */
  char *version_constraint;    /* Version constraint (NULL if none) */
  struct module_dependency *next;
};

struct exported_symbol {
  char *name;                  /* Symbol name */
  struct exported_symbol *next;
};

struct imported_symbol {
  char *name;                  /* Symbol name */
  char *from_module;           /* Module it's imported from */
  struct imported_symbol *next;
};

/* AST node definition structures for %ast_node directives */
struct ast_field {
  char *type;                  /* C type of the field (e.g., "int", "struct Expr *") */
  char *name;                  /* Field name */
  struct ast_field *next;      /* Next field in the list */
};

struct ast_node_def {
  char *name;                  /* Node type name (e.g., "Expr") */
  struct ast_field *fields;    /* Linked list of fields */
  int tag;                     /* Enum tag value (assigned during code generation) */
  int is_list;                 /* True if this is a %ast_list node */
  char *element_type;          /* For list nodes: the element type name */
  struct ast_node_def *next;   /* Next node definition */
};

/* The state vector for the entire parser generator is recorded as
** follows.  (LEMON uses no global variables and makes little use of
** static variables.  Fields in the following structure can be thought
** of as begin global variables in the program.) */
struct lime {
  struct state **sorted;   /* Table of states sorted by state number */
  struct rule *rule;       /* List of all rules */
  struct rule *startRule;  /* First rule */
  int nstate;              /* Number of states */
  int nxstate;             /* nstate with tail degenerate states removed */
  int nrule;               /* Number of rules */
  int nruleWithAction;     /* Number of rules with actions */
  int nsymbol;             /* Number of terminal and nonterminal symbols */
  int nterminal;           /* Number of terminal symbols */
  int minShiftReduce;      /* Minimum shift-reduce action value */
  int errAction;           /* Error action value */
  int accAction;           /* Accept action value */
  int noAction;            /* No-op action value */
  int minReduce;           /* Minimum reduce action */
  int maxAction;           /* Maximum action value of any kind */
  struct symbol **symbols; /* Sorted array of pointers to symbols */
  int errorcnt;            /* Number of errors */
  struct symbol *errsym;   /* The error symbol */
  struct symbol *wildcard; /* Token that matches anything */
  char *name;              /* Name of the generated parser */
  char *arg;               /* Declaration of the 3rd argument to parser */
  char *ctx;               /* Declaration of 2nd argument to constructor */
  char *tokentype;         /* Type of terminal symbols in the parser stack */
  char *vartype;           /* The default type of non-terminal symbols */
  char *start;             /* Name of the start symbol for the grammar */
  char *stacksize;         /* Size of the parser stack */
  char *include;           /* Code to put at the start of the C file */
  char *error;             /* Code to execute when an error is seen */
  char *overflow;          /* Code to execute on a stack overflow */
  char *failure;           /* Code to execute on parser failure */
  char *accept;            /* Code to execute when the parser excepts */
  char *extracode;         /* Code appended to the generated file */
  char *tokendest;         /* Code to execute to destroy token data */
  char *vardest;           /* Code for the default non-terminal destructor */
  char *filename;          /* Name of the input file */
  char *outname;           /* Name of the current output file */
  char *tokenprefix;       /* A prefix added to token names in the .h file */
  char *symbolprefix;      /* %symbol_prefix STR -- prefixes every internal
                           ** YY_* macro and yy* type/function name in the
                           ** emitted .c so two grammars combined into one
                           ** translation unit (or examined via nm) do not
                           ** collide.  See docs/lime_grammar(5).  Berkeley DB
                           ** style: e.g. %symbol_prefix CB_ renames yyParser
                           ** to CB_yyParser, YYNTOKEN to CB_YYNTOKEN, etc. */
  char *stackSizeLimit;    /* Function to return the stack size limit */
  char *reallocFunc;       /* Function to use to allocate stack space */
  char *freeFunc;          /* Function to use to free stack space */
  int nconflict;           /* Number of parsing conflicts */
  int nactiontab;          /* Number of entries in the yy_action[] table */
  int nlookaheadtab;       /* Number of entries in yy_lookahead[] */
  int tablesize;           /* Total table size of all tables in bytes */
  int basisflag;           /* Print only basis configurations */
  int printPreprocessed;   /* Show preprocessor output on stdout */
  int has_fallback;        /* True if any %fallback is seen in the grammar */
  int nolinenosflag;       /* True if #line statements should not be printed */
  int argc;                /* Number of command-line arguments */
  char **argv;             /* Command-line arguments */
  /* Snapshot support: copies of the computed action tables, filled by
  ** ReportTable() when snapshot mode is active. */
  int *aAction;            /* yy_action[] values */
  int *aLookahead;         /* yy_lookahead[] values */
  int nLookahead;          /* Total size of yy_lookahead including padding */
  /* Module composition metadata (NULL for monolithic grammars) */
  char *module_name;       /* Module name */
  char *module_version;    /* Module version (semantic) */
  char *module_description; /* Optional description */
  struct module_dependency *dependencies; /* Linked list of dependencies */
  struct exported_symbol *exports;        /* Linked list of exported symbols */
  struct imported_symbol *imports;        /* Linked list of imported symbols */
  /* %expect directive: expected number of conflicts, -1 if unset */
  int nexpect;
  /* File-level comment captured verbatim from the input.  Holds
  ** everything between byte 0 and the first non-comment,
  ** non-whitespace byte.  Used by the formatter (`lime -F`) to
  ** round-trip copyright headers, IDENTIFICATION blocks, and
  ** any other top-of-file documentation that the parser would
  ** otherwise discard.  See Lime-Letter-18.  NULL when the file
  ** starts with a directive at byte 0. */
  char *header_comment;
  /* %locations directive: true if location tracking is enabled */
  int has_locations;
  /* %location_type {Type} -- override the type used for source
  ** locations.  Defaults to LimeLocation (and Lime emits an include
  ** of lime_location.h).  Settable to e.g. {int} for callers (like
  ** PostgreSQL's gram.y, whose YYLTYPE is a byte offset) that need a
  ** scalar location type.  When set, Lime emits the user-supplied
  ** type as YYLOCATIONTYPE and skips the lime_location.h include --
  ** the caller is expected to provide the type definition. */
  char *location_type;
  /* %first_token directive: offset added to externally-visible terminal
  ** token codes.  Default 0 (terminals start at 1, Lemon convention).
  ** Set to 258 for Bison parity (reserves 0..127 for ASCII characters
  ** and 128..257 as a buffer).  When non-zero:
  **   - ReportHeader emits  #define <NAME>  <i + first_token>
  **   - The runtime template subtracts first_token from incoming
  **     yymajor before indexing the action table.  EOF (yymajor==0)
  **     is preserved unchanged.
  **   - User code (e.g. %syntax_error) sees the external value.
  ** Internal indices [1..nterminal-1] are unchanged. */
  int first_token;
  /* %error_sync directive: list of sync tokens for panic-mode recovery */
  char **error_sync_tokens;
  int n_error_sync_tokens;
  /* AST generation support */
  char *ast_prefix;                      /* Prefix for AST types (e.g., "Ast") */
  struct ast_node_def *ast_nodes;        /* Linked list of AST node defs */
  int ast_auto;                          /* True if %ast_auto is active */
  /* Lime-Letter-19: per-directive leading comments captured by the
  ** parser, emitted by `lime -F` immediately before the matching
  ** directive.  Naming mirrors the existing field (`name_comment`
  ** for `name`, `nexpect_comment` for `nexpect`, etc.).  NULL when
  ** the source had no comment immediately preceding the directive.
  ** Per-token / per-type comment slots are intentionally absent;
  ** those blocks emit in symbol-index order, not source order, and
  ** preserving per-symbol comments needs a bigger refactor than
  ** v0.3.2 should carry. */
  char *name_comment;
  char *tokentype_comment;
  char *arg_comment;          /* `extra_argument` */
  char *ctx_comment;          /* `extra_context`  */
  char *vartype_comment;      /* `default_type`   */
  char *start_comment;        /* `start_symbol` / `start` */
  char *stacksize_comment;
  char *tokenprefix_comment;
  char *symbolprefix_comment;
  char *nexpect_comment;      /* `expect` */
  char *include_comment;
  char *error_comment;        /* `syntax_error`  */
  char *failure_comment;      /* `parse_failure` */
  char *accept_comment;       /* `parse_accept`  */
  char *overflow_comment;     /* `stack_overflow` */
  char *tokendest_comment;    /* `token_destructor`   */
  char *vardest_comment;      /* `default_destructor` */
  char *module_comment;       /* covers the module_name/version/description block */
  /* Lime-Letter-19: any comments captured AFTER the last rule but
  ** before EOF (typically a closing `// vim:...` modeline or a
  ** trailing banner comment).  Emitted verbatim by `lime -F`
  ** after the final rule.  NULL when the source ended cleanly. */
  char *trailing_comment;
  /* Lime-Letter-21: declaration-order singly-linked lists of
  ** %token / %type groups, populated by the parser as each
  ** directive is recognized and consumed by `lime -F` to emit
  ** sections that preserve PG's section-banner comments (the
  ** ~120-150 lines lost on gram.lime in v0.3.4).  NULL for
  ** grammars with no %token / %type directives -- the formatter
  ** falls back to a symbol-index-order emit in that case. */
  LimeTokenGroup *first_token_group;
  LimeTokenGroup *last_token_group;   /* tail for O(1) append */
  LimeTypeGroup  *first_type_group;
  LimeTypeGroup  *last_type_group;
};

#define MemoryCheck(X) if((X)==0){ \
  extern void memory_error(); \
  memory_error(); \
}

/**************** From the file "table.h" *********************************/
/*
** All code in this file has been automatically generated
** from a specification in the file
**              "table.q"
** by the associative array code building program "aagen".
** Do not edit this file!  Instead, edit the specification
** file, then rerun aagen.
*/
/*
** Code for processing tables in the LEMON parser generator.
*/
/* Routines for handling a strings */

const char *Strsafe(const char *);

void Strsafe_init(void);
int Strsafe_insert(const char *);
const char *Strsafe_find(const char *);

/* Routines for handling symbols of the grammar */

struct symbol *Symbol_new(const char *);
int Symbolcmpp(const void *, const void *);
void Symbol_init(void);
int Symbol_insert(struct symbol *, const char *);
struct symbol *Symbol_find(const char *);
struct symbol *Symbol_Nth(int);
int Symbol_count(void);
struct symbol **Symbol_arrayof(void);

/* Routines to manage the state table */

int Configcmp(const char *, const char *);
struct state *State_new(void);
void State_init(void);
int State_insert(struct state *, struct config *);
struct state *State_find(struct config *);
struct state **State_arrayof(void);

/* Routines used for efficiency in Configlist_add */

void Configtable_init(void);
int Configtable_insert(struct config *);
struct config *Configtable_find(struct config *);
void Configtable_clear(int(*)(struct config *));

/****************** From the file "action.c" *******************************/
/*
** Routines processing parser actions in the LEMON parser generator.
*/

/* Allocate a new parser action */
static struct action *Action_new(void){
  static struct action *actionfreelist = 0;
  struct action *newaction;

  if( actionfreelist==0 ){
    int i;
    int amt = 100;
    actionfreelist = (struct action *)lime_calloc(amt, sizeof(struct action));
    if( actionfreelist==0 ){
      fprintf(stderr,"Unable to allocate memory for a new parser action.");
      exit(1);
    }
    for(i=0; i<amt-1; i++) actionfreelist[i].next = &actionfreelist[i+1];
    actionfreelist[amt-1].next = 0;
  }
  newaction = actionfreelist;
  actionfreelist = actionfreelist->next;
  return newaction;
}

/* Compare two actions for sorting purposes.  Return negative, zero, or
** positive if the first action is less than, equal to, or greater than
** the first
*/
static int actioncmp(
  struct action *ap1,
  struct action *ap2
){
  int rc;
  rc = ap1->sp->index - ap2->sp->index;
  if( rc==0 ){
    rc = (int)ap1->type - (int)ap2->type;
  }
  if( rc==0 && (ap1->type==REDUCE || ap1->type==SHIFTREDUCE) ){
    rc = ap1->x.rp->index - ap2->x.rp->index;
  }
  if( rc==0 ){
    rc = (int) (ap2 - ap1);
  }
  return rc;
}

/* Sort parser actions */
static struct action *Action_sort(
  struct action *ap
){
  ap = (struct action *)msort((char *)ap,(char **)&ap->next,
                              (int(*)(const char*,const char*))actioncmp);
  return ap;
}

void Action_add(
  struct action **app,
  enum e_action type,
  struct symbol *sp,
  char *arg
){
  struct action *newaction;
  newaction = Action_new();
  newaction->next = *app;
  *app = newaction;
  newaction->type = type;
  newaction->sp = sp;
  newaction->spOpt = 0;
  if( type==SHIFT ){
    newaction->x.stp = (struct state *)arg;
  }else{
    newaction->x.rp = (struct rule *)arg;
  }
}
/********************** New code to implement the "acttab" module ***********/
/*
** This module implements routines use to construct the yy_action[] table.
*/

/*
** The state of the yy_action table under construction is an instance of
** the following structure.
**
** The yy_action table maps the pair (state_number, lookahead) into an
** action_number.  The table is an array of integers pairs.  The state_number
** determines an initial offset into the yy_action array.  The lookahead
** value is then added to this initial offset to get an index X into the
** yy_action array. If the aAction[X].lookahead equals the value of the
** of the lookahead input, then the value of the action_number output is
** aAction[X].action.  If the lookaheads do not match then the
** default action for the state_number is returned.
**
** All actions associated with a single state_number are first entered
** into aLookahead[] using multiple calls to acttab_action().  Then the
** actions for that single state_number are placed into the aAction[]
** array with a single call to acttab_insert().  The acttab_insert() call
** also resets the aLookahead[] array in preparation for the next
** state number.
*/
struct lookahead_action {
  int lookahead;             /* Value of the lookahead token */
  int action;                /* Action to take on the given lookahead */
};
typedef struct acttab acttab;
struct acttab {
  int nAction;                 /* Number of used slots in aAction[] */
  int nActionAlloc;            /* Slots allocated for aAction[] */
  struct lookahead_action
    *aAction,                  /* The yy_action[] table under construction */
    *aLookahead;               /* A single new transaction set */
  int mnLookahead;             /* Minimum aLookahead[].lookahead */
  int mnAction;                /* Action associated with mnLookahead */
  int mxLookahead;             /* Maximum aLookahead[].lookahead */
  int nLookahead;              /* Used slots in aLookahead[] */
  int nLookaheadAlloc;         /* Slots allocated in aLookahead[] */
  int nterminal;               /* Number of terminal symbols */
  int nsymbol;                 /* total number of symbols */
};

/* Return the number of entries in the yy_action table */
#define acttab_lookahead_size(X) ((X)->nAction)

/* The value for the N-th entry in yy_action */
#define acttab_yyaction(X,N)  ((X)->aAction[N].action)

/* The value for the N-th entry in yy_lookahead */
#define acttab_yylookahead(X,N)  ((X)->aAction[N].lookahead)

/* Free all memory associated with the given acttab */
void acttab_free(acttab *p){
  lime_free( p->aAction );
  lime_free( p->aLookahead );
  lime_free( p );
}

/* Allocate a new acttab structure */
acttab *acttab_alloc(int nsymbol, int nterminal){
  acttab *p = (acttab *) lime_calloc( 1, sizeof(*p) );
  if( p==0 ){
    fprintf(stderr,"Unable to allocate memory for a new acttab.");
    exit(1);
  }
  memset(p, 0, sizeof(*p));
  p->nsymbol = nsymbol;
  p->nterminal = nterminal;
  return p;
}

/* Add a new action to the current transaction set.
**
** This routine is called once for each lookahead for a particular
** state.
*/
void acttab_action(acttab *p, int lookahead, int action){
  if( p->nLookahead>=p->nLookaheadAlloc ){
    p->nLookaheadAlloc += 25;
    p->aLookahead = (struct lookahead_action *) lime_realloc( p->aLookahead,
                             sizeof(p->aLookahead[0])*p->nLookaheadAlloc );
    if( p->aLookahead==0 ){
      fprintf(stderr,"malloc failed\n");
      exit(1);
    }
  }
  if( p->nLookahead==0 ){
    p->mxLookahead = lookahead;
    p->mnLookahead = lookahead;
    p->mnAction = action;
  }else{
    if( p->mxLookahead<lookahead ) p->mxLookahead = lookahead;
    if( p->mnLookahead>lookahead ){
      p->mnLookahead = lookahead;
      p->mnAction = action;
    }
  }
  p->aLookahead[p->nLookahead].lookahead = lookahead;
  p->aLookahead[p->nLookahead].action = action;
  p->nLookahead++;
}

/*
** Add the transaction set built up with prior calls to acttab_action()
** into the current action table.  Then reset the transaction set back
** to an empty set in preparation for a new round of acttab_action() calls.
**
** Return the offset into the action table of the new transaction.
**
** If the makeItSafe parameter is true, then the offset is chosen so that
** it is impossible to overread the yy_lookaside[] table regardless of
** the lookaside token.  This is done for the terminal symbols, as they
** come from external inputs and can contain syntax errors.  When makeItSafe
** is false, there is more flexibility in selecting offsets, resulting in
** a smaller table.  For non-terminal symbols, which are never syntax errors,
** makeItSafe can be false.
*/
int acttab_insert(acttab *p, int makeItSafe){
  int i, j, k, n, end;
  assert( p->nLookahead>0 );

  /* Make sure we have enough space to hold the expanded action table
  ** in the worst case.  The worst case occurs if the transaction set
  ** must be appended to the current action table
  */
  n = p->nsymbol + 1;
  if( p->nAction + n >= p->nActionAlloc ){
    int oldAlloc = p->nActionAlloc;
    p->nActionAlloc = p->nAction + n + p->nActionAlloc + 20;
    p->aAction = (struct lookahead_action *) lime_realloc( p->aAction,
                          sizeof(p->aAction[0])*p->nActionAlloc);
    if( p->aAction==0 ){
      fprintf(stderr,"malloc failed\n");
      exit(1);
    }
    for(i=oldAlloc; i<p->nActionAlloc; i++){
      p->aAction[i].lookahead = -1;
      p->aAction[i].action = -1;
    }
  }

  /* Scan the existing action table looking for an offset that is a
  ** duplicate of the current transaction set.  Fall out of the loop
  ** if and when the duplicate is found.
  **
  ** i is the index in p->aAction[] where p->mnLookahead is inserted.
  */
  end = makeItSafe ? p->mnLookahead : 0;
  for(i=p->nAction-1; i>=end; i--){
    if( p->aAction[i].lookahead==p->mnLookahead ){
      /* All lookaheads and actions in the aLookahead[] transaction
      ** must match against the candidate aAction[i] entry. */
      if( p->aAction[i].action!=p->mnAction ) continue;
      for(j=0; j<p->nLookahead; j++){
        k = p->aLookahead[j].lookahead - p->mnLookahead + i;
        if( k<0 || k>=p->nAction ) break;
        if( p->aLookahead[j].lookahead!=p->aAction[k].lookahead ) break;
        if( p->aLookahead[j].action!=p->aAction[k].action ) break;
      }
      if( j<p->nLookahead ) continue;

      /* No possible lookahead value that is not in the aLookahead[]
      ** transaction is allowed to match aAction[i] */
      n = 0;
      for(j=0; j<p->nAction; j++){
        if( p->aAction[j].lookahead<0 ) continue;
        if( p->aAction[j].lookahead==j+p->mnLookahead-i ) n++;
      }
      if( n==p->nLookahead ){
        break;  /* An exact match is found at offset i */
      }
    }
  }

  /* If no existing offsets exactly match the current transaction, find an
  ** an empty offset in the aAction[] table in which we can add the
  ** aLookahead[] transaction.
  */
  if( i<end ){
    /* Look for holes in the aAction[] table that fit the current
    ** aLookahead[] transaction.  Leave i set to the offset of the hole.
    ** If no holes are found, i is left at p->nAction, which means the
    ** transaction will be appended. */
    i = makeItSafe ? p->mnLookahead : 0;
    for(; i<p->nActionAlloc - p->mxLookahead; i++){
      if( p->aAction[i].lookahead<0 ){
        for(j=0; j<p->nLookahead; j++){
          k = p->aLookahead[j].lookahead - p->mnLookahead + i;
          if( k<0 ) break;
          if( p->aAction[k].lookahead>=0 ) break;
        }
        if( j<p->nLookahead ) continue;
        for(j=0; j<p->nAction; j++){
          if( p->aAction[j].lookahead==j+p->mnLookahead-i ) break;
        }
        if( j==p->nAction ){
          break;  /* Fits in empty slots */
        }
      }
    }
  }
  /* Insert transaction set at index i. */
#if 0
  printf("Acttab:");
  for(j=0; j<p->nLookahead; j++){
    printf(" %d", p->aLookahead[j].lookahead);
  }
  printf(" inserted at %d\n", i);
#endif
  for(j=0; j<p->nLookahead; j++){
    k = p->aLookahead[j].lookahead - p->mnLookahead + i;
    p->aAction[k] = p->aLookahead[j];
    if( k>=p->nAction ) p->nAction = k+1;
  }
  if( makeItSafe && i+p->nterminal>=p->nAction ) p->nAction = i+p->nterminal+1;
  p->nLookahead = 0;

  /* Return the offset that is added to the lookahead in order to get the
  ** index into yy_action of the action */
  return i - p->mnLookahead;
}

/*
** Return the size of the action table without the trailing syntax error
** entries.
*/
int acttab_action_size(acttab *p){
  int n = p->nAction;
  while( n>0 && p->aAction[n-1].lookahead<0 ){ n--; }
  return n;
}

/********************** From the file "build.c" *****************************/
/*
** Routines to construction the finite state machine for the LEMON
** parser generator.
*/

/* Find a precedence symbol of every rule in the grammar.
**
** Those rules which have a precedence symbol coded in the input
** grammar using the "[symbol]" construct will already have the
** rp->precsym field filled.  Other rules take as their precedence
** symbol the first RHS symbol with a defined precedence.  If there
** are not RHS symbols with a defined precedence, the precedence
** symbol field is left blank.
*/
void FindRulePrecedences(struct lime *xp)
{
  struct rule *rp;
  for(rp=xp->rule; rp; rp=rp->next){
    if( rp->precsym==0 ){
      int i, j;
      for(i=0; i<rp->nrhs && rp->precsym==0; i++){
        struct symbol *sp = rp->rhs[i];
        if( sp->type==MULTITERMINAL ){
          for(j=0; j<sp->nsubsym; j++){
            if( sp->subsym[j]->prec>=0 ){
              rp->precsym = sp->subsym[j];
              break;
            }
          }
        }else if( sp->prec>=0 ){
          rp->precsym = rp->rhs[i];
        }
      }
    }
  }
  return;
}

/* Find all nonterminals which will generate the empty string.
** Then go back and compute the first sets of every nonterminal.
** The first set is the set of all terminal symbols which can begin
** a string generated by that nonterminal.
*/
void FindFirstSets(struct lime *lemp)
{
  int i, j;
  struct rule *rp;
  int progress;

  for(i=0; i<lemp->nsymbol; i++){
    lemp->symbols[i]->lambda = LEMON_FALSE;
  }
  for(i=lemp->nterminal; i<lemp->nsymbol; i++){
    lemp->symbols[i]->firstset = SetNew();
  }

  /* First compute all lambdas */
  do{
    progress = 0;
    for(rp=lemp->rule; rp; rp=rp->next){
      if( rp->lhs->lambda ) continue;
      for(i=0; i<rp->nrhs; i++){
        struct symbol *sp = rp->rhs[i];
        assert( sp->type==NONTERMINAL || sp->lambda==LEMON_FALSE );
        if( sp->lambda==LEMON_FALSE ) break;
      }
      if( i==rp->nrhs ){
        rp->lhs->lambda = LEMON_TRUE;
        progress = 1;
      }
    }
  }while( progress );

  /* Now compute all first sets */
  do{
    struct symbol *s1, *s2;
    progress = 0;
    for(rp=lemp->rule; rp; rp=rp->next){
      s1 = rp->lhs;
      for(i=0; i<rp->nrhs; i++){
        s2 = rp->rhs[i];
        if( s2->type==TERMINAL ){
          progress += SetAdd(s1->firstset,s2->index);
          break;
        }else if( s2->type==MULTITERMINAL ){
          for(j=0; j<s2->nsubsym; j++){
            progress += SetAdd(s1->firstset,s2->subsym[j]->index);
          }
          break;
        }else if( s1==s2 ){
          if( s1->lambda==LEMON_FALSE ) break;
        }else{
          progress += SetUnion(s1->firstset,s2->firstset);
          if( s2->lambda==LEMON_FALSE ) break;
        }
      }
    }
  }while( progress );
  return;
}

/* Compute all LR(0) states for the grammar.  Links
** are added to between some states so that the LR(1) follow sets
** can be computed later.
*/
PRIVATE struct state *getstate(struct lime *);  /* forward reference */
void FindStates(struct lime *lemp)
{
  struct symbol *sp;
  struct rule *rp;

  Configlist_init();

  /* Find the start symbol */
  if( lemp->start ){
    sp = Symbol_find(lemp->start);
    if( sp==0 ){
      ErrorMsg(lemp->filename,0,
        "The specified start symbol \"%s\" is not "
        "in a nonterminal of the grammar.  \"%s\" will be used as the start "
        "symbol instead.",lemp->start,lemp->startRule->lhs->name);
      lemp->errorcnt++;
      sp = lemp->startRule->lhs;
    }
  }else if( lemp->startRule ){
    sp = lemp->startRule->lhs;
  }else{
    ErrorMsg(lemp->filename,0,"Internal error - no start rule\n");
    exit(1);
  }

  /* Make sure the start symbol doesn't occur on the right-hand side of
  ** any rule.  Report an error if it does.  (YACC would generate a new
  ** start symbol in this case.) */
  for(rp=lemp->rule; rp; rp=rp->next){
    int i;
    for(i=0; i<rp->nrhs; i++){
      if( rp->rhs[i]==sp ){   /* FIX ME:  Deal with multiterminals */
        ErrorMsg(lemp->filename,0,
          "The start symbol \"%s\" occurs on the "
          "right-hand side of a rule. This will result in a parser which "
          "does not work properly.",sp->name);
        lemp->errorcnt++;
      }
    }
  }

  /* The basis configuration set for the first state
  ** is all rules which have the start symbol as their
  ** left-hand side */
  for(rp=sp->rule; rp; rp=rp->nextlhs){
    struct config *newcfp;
    rp->lhsStart = 1;
    newcfp = Configlist_addbasis(rp,0);
    SetAdd(newcfp->fws,0);
  }

  /* Compute the first state.  All other states will be
  ** computed automatically during the computation of the first one.
  ** The returned pointer to the first state is not used. */
  (void)getstate(lemp);
  return;
}

/* Return a pointer to a state which is described by the configuration
** list which has been built from calls to Configlist_add.
*/
PRIVATE void buildshifts(struct lime *, struct state *); /* Forwd ref */
PRIVATE struct state *getstate(struct lime *lemp)
{
  struct config *cfp, *bp;
  struct state *stp;

  /* Extract the sorted basis of the new state.  The basis was constructed
  ** by prior calls to "Configlist_addbasis()". */
  Configlist_sortbasis();
  bp = Configlist_basis();

  /* Get a state with the same basis */
  stp = State_find(bp);
  if( stp ){
    /* A state with the same basis already exists!  Copy all the follow-set
    ** propagation links from the state under construction into the
    ** preexisting state, then return a pointer to the preexisting state */
    struct config *x, *y;
    for(x=bp, y=stp->bp; x && y; x=x->bp, y=y->bp){
      Plink_copy(&y->bplp,x->bplp);
      Plink_delete(x->fplp);
      x->fplp = x->bplp = 0;
    }
    cfp = Configlist_return();
    Configlist_eat(cfp);
  }else{
    /* This really is a new state.  Construct all the details */
    Configlist_closure(lemp);    /* Compute the configuration closure */
    Configlist_sort();           /* Sort the configuration closure */
    cfp = Configlist_return();   /* Get a pointer to the config list */
    stp = State_new();           /* A new state structure */
    MemoryCheck(stp);
    stp->bp = bp;                /* Remember the configuration basis */
    stp->cfp = cfp;              /* Remember the configuration closure */
    stp->statenum = lemp->nstate++; /* Every state gets a sequence number */
    stp->ap = 0;                 /* No actions, yet. */
    State_insert(stp,stp->bp);   /* Add to the state table */
    buildshifts(lemp,stp);       /* Recursively compute successor states */
  }
  return stp;
}

/*
** Return true if two symbols are the same.
*/
int same_symbol(struct symbol *a, struct symbol *b)
{
  int i;
  if( a==b ) return 1;
  if( a->type!=MULTITERMINAL ) return 0;
  if( b->type!=MULTITERMINAL ) return 0;
  if( a->nsubsym!=b->nsubsym ) return 0;
  for(i=0; i<a->nsubsym; i++){
    if( a->subsym[i]!=b->subsym[i] ) return 0;
  }
  return 1;
}

/* Construct all successor states to the given state.  A "successor"
** state is any state which can be reached by a shift action.
*/
PRIVATE void buildshifts(struct lime *lemp, struct state *stp)
{
  struct config *cfp;  /* For looping thru the config closure of "stp" */
  struct config *bcfp; /* For the inner loop on config closure of "stp" */
  struct config *newcfg;  /* */
  struct symbol *sp;   /* Symbol following the dot in configuration "cfp" */
  struct symbol *bsp;  /* Symbol following the dot in configuration "bcfp" */
  struct state *newstp; /* A pointer to a successor state */

  /* Each configuration becomes complete after it contributes to a successor
  ** state.  Initially, all configurations are incomplete */
  for(cfp=stp->cfp; cfp; cfp=cfp->next) cfp->status = INCOMPLETE;

  /* Loop through all configurations of the state "stp" */
  for(cfp=stp->cfp; cfp; cfp=cfp->next){
    if( cfp->status==COMPLETE ) continue;    /* Already used by inner loop */
    if( cfp->dot>=cfp->rp->nrhs ) continue;  /* Can't shift this config */
    Configlist_reset();                      /* Reset the new config set */
    sp = cfp->rp->rhs[cfp->dot];             /* Symbol after the dot */

    /* For every configuration in the state "stp" which has the symbol "sp"
    ** following its dot, add the same configuration to the basis set under
    ** construction but with the dot shifted one symbol to the right. */
    for(bcfp=cfp; bcfp; bcfp=bcfp->next){
      if( bcfp->status==COMPLETE ) continue;    /* Already used */
      if( bcfp->dot>=bcfp->rp->nrhs ) continue; /* Can't shift this one */
      bsp = bcfp->rp->rhs[bcfp->dot];           /* Get symbol after dot */
      if( !same_symbol(bsp,sp) ) continue;      /* Must be same as for "cfp" */
      bcfp->status = COMPLETE;                  /* Mark this config as used */
      newcfg = Configlist_addbasis(bcfp->rp,bcfp->dot+1);
      Plink_add(&newcfg->bplp,bcfp);
    }

    /* Get a pointer to the state described by the basis configuration set
    ** constructed in the preceding loop */
    newstp = getstate(lemp);

    /* The state "newstp" is reached from the state "stp" by a shift action
    ** on the symbol "sp" */
    if( sp->type==MULTITERMINAL ){
      int i;
      for(i=0; i<sp->nsubsym; i++){
        Action_add(&stp->ap,SHIFT,sp->subsym[i],(char*)newstp);
      }
    }else{
      Action_add(&stp->ap,SHIFT,sp,(char *)newstp);
    }
  }
}

/*
** Construct the propagation links
*/
void FindLinks(struct lime *lemp)
{
  int i;
  struct config *cfp, *other;
  struct state *stp;
  struct plink *plp;

  /* Housekeeping detail:
  ** Add to every propagate link a pointer back to the state to
  ** which the link is attached. */
  for(i=0; i<lemp->nstate; i++){
    stp = lemp->sorted[i];
    for(cfp=stp?stp->cfp:0; cfp; cfp=cfp->next){
      cfp->stp = stp;
    }
  }

  /* Convert all backlinks into forward links.  Only the forward
  ** links are used in the follow-set computation. */
  for(i=0; i<lemp->nstate; i++){
    stp = lemp->sorted[i];
    for(cfp=stp?stp->cfp:0; cfp; cfp=cfp->next){
      for(plp=cfp->bplp; plp; plp=plp->next){
        other = plp->cfp;
        Plink_add(&other->fplp,cfp);
      }
    }
  }
}

/* Compute all followsets.
**
** A followset is the set of all symbols which can come immediately
** after a configuration.
*/
void FindFollowSets(struct lime *lemp)
{
  int i;
  struct config *cfp;
  struct plink *plp;
  int progress;
  int change;

  for(i=0; i<lemp->nstate; i++){
    assert( lemp->sorted[i]!=0 );
    for(cfp=lemp->sorted[i]->cfp; cfp; cfp=cfp->next){
      cfp->status = INCOMPLETE;
    }
  }

  do{
    progress = 0;
    for(i=0; i<lemp->nstate; i++){
      assert( lemp->sorted[i]!=0 );
      for(cfp=lemp->sorted[i]->cfp; cfp; cfp=cfp->next){
        if( cfp->status==COMPLETE ) continue;
        for(plp=cfp->fplp; plp; plp=plp->next){
          change = SetUnion(plp->cfp->fws,cfp->fws);
          if( change ){
            plp->cfp->status = INCOMPLETE;
            progress = 1;
          }
        }
        cfp->status = COMPLETE;
      }
    }
  }while( progress );
}

static int resolve_conflict(struct action *,struct action *);

/* Compute the reduce actions, and resolve conflicts.
*/
void FindActions(struct lime *lemp)
{
  int i,j;
  struct config *cfp;
  struct state *stp;
  struct symbol *sp;
  struct rule *rp;

  /* Add all of the reduce actions
  ** A reduce action is added for each element of the followset of
  ** a configuration which has its dot at the extreme right.
  */
  for(i=0; i<lemp->nstate; i++){   /* Loop over all states */
    stp = lemp->sorted[i];
    for(cfp=stp->cfp; cfp; cfp=cfp->next){  /* Loop over all configurations */
      if( cfp->rp->nrhs==cfp->dot ){        /* Is dot at extreme right? */
        for(j=0; j<lemp->nterminal; j++){
          if( SetFind(cfp->fws,j) ){
            /* Add a reduce action to the state "stp" which will reduce by the
            ** rule "cfp->rp" if the lookahead symbol is "lemp->symbols[j]" */
            Action_add(&stp->ap,REDUCE,lemp->symbols[j],(char *)cfp->rp);
          }
        }
      }
    }
  }

  /* Add the accepting token */
  if( lemp->start ){
    sp = Symbol_find(lemp->start);
    if( sp==0 ){
      if( lemp->startRule==0 ){
        fprintf(stderr, "internal error on source line %d: no start rule\n",
                __LINE__);
        exit(1);
      }
      sp = lemp->startRule->lhs;
    }
  }else{
    sp = lemp->startRule->lhs;
  }
  /* Add to the first state (which is always the starting state of the
  ** finite state machine) an action to ACCEPT if the lookahead is the
  ** start nonterminal.  */
  Action_add(&lemp->sorted[0]->ap,ACCEPT,sp,0);

  /* Resolve conflicts */
  for(i=0; i<lemp->nstate; i++){
    struct action *ap, *nap;
    stp = lemp->sorted[i];
    /* assert( stp->ap ); */
    stp->ap = Action_sort(stp->ap);
    for(ap=stp->ap; ap && ap->next; ap=ap->next){
      for(nap=ap->next; nap && nap->sp==ap->sp; nap=nap->next){
         /* The two actions "ap" and "nap" have the same lookahead.
         ** Figure out which one should be used */
         lemp->nconflict += resolve_conflict(ap,nap);
      }
    }
  }

  /* Report an error for each rule that can never be reduced. */
  for(rp=lemp->rule; rp; rp=rp->next) rp->canReduce = LEMON_FALSE;
  for(i=0; i<lemp->nstate; i++){
    struct action *ap;
    for(ap=lemp->sorted[i]->ap; ap; ap=ap->next){
      if( ap->type==REDUCE ) ap->x.rp->canReduce = LEMON_TRUE;
    }
  }
  for(rp=lemp->rule; rp; rp=rp->next){
    if( rp->canReduce ) continue;
    ErrorMsg(lemp->filename,rp->ruleline,"This rule can not be reduced.\n");
    lemp->errorcnt++;
  }
}

/* Resolve a conflict between the two given actions.  If the
** conflict can't be resolved, return non-zero.
**
** NO LONGER TRUE:
**   To resolve a conflict, first look to see if either action
**   is on an error rule.  In that case, take the action which
**   is not associated with the error rule.  If neither or both
**   actions are associated with an error rule, then try to
**   use precedence to resolve the conflict.
**
** If either action is a SHIFT, then it must be apx.  This
** function won't work if apx->type==REDUCE and apy->type==SHIFT.
*/
static int resolve_conflict(
  struct action *apx,
  struct action *apy
){
  struct symbol *spx, *spy;
  int errcnt = 0;
  assert( apx->sp==apy->sp );  /* Otherwise there would be no conflict */
  if( apx->type==SHIFT && apy->type==SHIFT ){
    apy->type = SSCONFLICT;
    errcnt++;
  }
  if( apx->type==SHIFT && apy->type==REDUCE ){
    spx = apx->sp;
    spy = apy->x.rp->precsym;
    if( spy==0 || spx->prec<0 || spy->prec<0 ){
      /* Not enough precedence information. */
      apy->type = SRCONFLICT;
      errcnt++;
    }else if( spx->prec>spy->prec ){    /* higher precedence wins */
      apy->type = RD_RESOLVED;
    }else if( spx->prec<spy->prec ){
      apx->type = SH_RESOLVED;
    }else if( spx->prec==spy->prec && spx->assoc==RIGHT ){ /* Use operator */
      apy->type = RD_RESOLVED;                             /* associativity */
    }else if( spx->prec==spy->prec && spx->assoc==LEFT ){  /* to break tie */
      apx->type = SH_RESOLVED;
    }else{
      assert( spx->prec==spy->prec && spx->assoc==NONE );
      apx->type = ERROR;
    }
  }else if( apx->type==REDUCE && apy->type==REDUCE ){
    spx = apx->x.rp->precsym;
    spy = apy->x.rp->precsym;
    if( spx==0 || spy==0 || spx->prec<0 ||
    spy->prec<0 || spx->prec==spy->prec ){
      apy->type = RRCONFLICT;
      errcnt++;
    }else if( spx->prec>spy->prec ){
      apy->type = RD_RESOLVED;
    }else if( spx->prec<spy->prec ){
      apx->type = RD_RESOLVED;
    }
  }else{
    assert(
      apx->type==SH_RESOLVED ||
      apx->type==RD_RESOLVED ||
      apx->type==SSCONFLICT ||
      apx->type==SRCONFLICT ||
      apx->type==RRCONFLICT ||
      apy->type==SH_RESOLVED ||
      apy->type==RD_RESOLVED ||
      apy->type==SSCONFLICT ||
      apy->type==SRCONFLICT ||
      apy->type==RRCONFLICT
    );
    /* The REDUCE/SHIFT case cannot happen because SHIFTs come before
    ** REDUCEs on the list.  If we reach this point it must be because
    ** the parser conflict had already been resolved. */
  }
  return errcnt;
}
/********************* From the file "configlist.c" *************************/
/*
** Routines to processing a configuration list and building a state
** in the LEMON parser generator.
*/

static struct config *freelist = 0;      /* List of free configurations */
static struct config *current = 0;       /* Top of list of configurations */
static struct config **currentend = 0;   /* Last on list of configs */
static struct config *basis = 0;         /* Top of list of basis configs */
static struct config **basisend = 0;     /* End of list of basis configs */

/* Return a pointer to a new configuration */
PRIVATE struct config *newconfig(void){
  return (struct config*)lime_calloc(1, sizeof(struct config));
}

/* The configuration "old" is no longer used */
PRIVATE void deleteconfig(struct config *old)
{
  old->next = freelist;
  freelist = old;
}

/* Initialized the configuration list builder */
void Configlist_init(void){
  current = 0;
  currentend = &current;
  basis = 0;
  basisend = &basis;
  Configtable_init();
  return;
}

/* Initialized the configuration list builder */
void Configlist_reset(void){
  current = 0;
  currentend = &current;
  basis = 0;
  basisend = &basis;
  Configtable_clear(0);
  return;
}

/* Add another configuration to the configuration list */
struct config *Configlist_add(
  struct rule *rp,    /* The rule */
  int dot             /* Index into the RHS of the rule where the dot goes */
){
  struct config *cfp, model;

  assert( currentend!=0 );
  model.rp = rp;
  model.dot = dot;
  cfp = Configtable_find(&model);
  if( cfp==0 ){
    cfp = newconfig();
    cfp->rp = rp;
    cfp->dot = dot;
    cfp->fws = SetNew();
    cfp->stp = 0;
    cfp->fplp = cfp->bplp = 0;
    cfp->next = 0;
    cfp->bp = 0;
    *currentend = cfp;
    currentend = &cfp->next;
    Configtable_insert(cfp);
  }
  return cfp;
}

/* Add a basis configuration to the configuration list */
struct config *Configlist_addbasis(struct rule *rp, int dot)
{
  struct config *cfp, model;

  assert( basisend!=0 );
  assert( currentend!=0 );
  model.rp = rp;
  model.dot = dot;
  cfp = Configtable_find(&model);
  if( cfp==0 ){
    cfp = newconfig();
    cfp->rp = rp;
    cfp->dot = dot;
    cfp->fws = SetNew();
    cfp->stp = 0;
    cfp->fplp = cfp->bplp = 0;
    cfp->next = 0;
    cfp->bp = 0;
    *currentend = cfp;
    currentend = &cfp->next;
    *basisend = cfp;
    basisend = &cfp->bp;
    Configtable_insert(cfp);
  }
  return cfp;
}

/* Compute the closure of the configuration list */
void Configlist_closure(struct lime *lemp)
{
  struct config *cfp, *newcfp;
  struct rule *rp, *newrp;
  struct symbol *sp, *xsp;
  int i, dot;

  assert( currentend!=0 );
  for(cfp=current; cfp; cfp=cfp->next){
    rp = cfp->rp;
    dot = cfp->dot;
    if( dot>=rp->nrhs ) continue;
    sp = rp->rhs[dot];
    if( sp->type==NONTERMINAL ){
      if( sp->rule==0 && sp!=lemp->errsym ){
        ErrorMsg(lemp->filename,rp->line,"Nonterminal \"%s\" has no rules.",
          sp->name);
        lemp->errorcnt++;
      }
      for(newrp=sp->rule; newrp; newrp=newrp->nextlhs){
        newcfp = Configlist_add(newrp,0);
        for(i=dot+1; i<rp->nrhs; i++){
          xsp = rp->rhs[i];
          if( xsp->type==TERMINAL ){
            SetAdd(newcfp->fws,xsp->index);
            break;
          }else if( xsp->type==MULTITERMINAL ){
            int k;
            for(k=0; k<xsp->nsubsym; k++){
              SetAdd(newcfp->fws, xsp->subsym[k]->index);
            }
            break;
          }else{
            SetUnion(newcfp->fws,xsp->firstset);
            if( xsp->lambda==LEMON_FALSE ) break;
          }
        }
        if( i==rp->nrhs ) Plink_add(&cfp->fplp,newcfp);
      }
    }
  }
  return;
}

/* Sort the configuration list */
void Configlist_sort(void){
  current = (struct config*)msort((char*)current,(char**)&(current->next),
                                  Configcmp);
  currentend = 0;
  return;
}

/* Sort the basis configuration list */
void Configlist_sortbasis(void){
  basis = (struct config*)msort((char*)current,(char**)&(current->bp),
                                Configcmp);
  basisend = 0;
  return;
}

/* Return a pointer to the head of the configuration list and
** reset the list */
struct config *Configlist_return(void){
  struct config *old;
  old = current;
  current = 0;
  currentend = 0;
  return old;
}

/* Return a pointer to the head of the configuration list and
** reset the list */
struct config *Configlist_basis(void){
  struct config *old;
  old = basis;
  basis = 0;
  basisend = 0;
  return old;
}

/* Free all elements of the given configuration list */
void Configlist_eat(struct config *cfp)
{
  struct config *nextcfp;
  for(; cfp; cfp=nextcfp){
    nextcfp = cfp->next;
    assert( cfp->fplp==0 );
    assert( cfp->bplp==0 );
    if( cfp->fws ) SetFree(cfp->fws);
    deleteconfig(cfp);
  }
  return;
}
/***************** From the file "error.c" *********************************/
/*
** Code for printing error message.
*/

void ErrorMsg(const char *filename, int lineno, const char *format, ...){
  va_list ap;
  fprintf(stderr, "%s:%d: ", filename, lineno);
  va_start(ap, format);
  vfprintf(stderr,format,ap);
  va_end(ap);
  fprintf(stderr, "\n");
}
/**************** From the file "main.c" ************************************/
/*
** Main program file for the LEMON parser generator.
*/

/* Report an out-of-memory condition and abort.  This function
** is used mostly by the "MemoryCheck" macro in struct.h
*/
void memory_error(void){
  fprintf(stderr,"Out of memory.  Aborting...\n");
  exit(1);
}

static int nDefine = 0;        /* Number of -D options on the command line */
static int nDefineUsed = 0;    /* Number of -D options actually used */
static char **azDefine = 0;    /* Name of the -D macros */
static char *bDefineUsed = 0;  /* True for every -D macro actually used */

/* This routine is called with the argument to each -D command-line option.
** Add the macro defined to the azDefine array.
**
** Lime v0.4.0: `-Ddialect=NAME` is recognized as shorthand for
** `-Ddialect_NAME` -- and ONLY for that key.  This pairs with the
** `%dialect NAME { ... }` directive (desugared in preprocess_input)
** so users write the same NAME on the CLI and in the grammar.
** All other `-D` invocations still take a bare macro name; the
** historical `=value` suffix is dropped exactly as before.  See
** docs/DIALECT.md.
*/
static void handle_D_option(char *z){
  char **paz;
  if( strncmp(z, "dialect=", 8)==0 ){
    const char *name = z + 8;
    size_t n = lemonStrlen((char*)name);
    size_t k;
    if( n==0 ){
      fprintf(stderr,"lime: -Ddialect= requires a name\n");
      exit(1);
    }
    if( !ISALPHA((unsigned char)name[0]) && name[0]!='_' ){
      fprintf(stderr,
        "lime: -Ddialect=%s: name must start with a letter or '_'\n",
        name);
      exit(1);
    }
    for(k=1; k<n; k++){
      if( !ISALNUM((unsigned char)name[k]) && name[k]!='_' ){
        fprintf(stderr,
          "lime: -Ddialect=%s: invalid character '%c' in name\n",
          name, name[k]);
        exit(1);
      }
    }
    /* Build "dialect_<name>" in a fresh buffer.  Re-points z so the
    ** rest of this function consumes the rewritten string; the
    ** original argv entry is left untouched.  Buffer is owned by
    ** the lime_malloc arena and freed at exit, like every other
    ** allocation in this file. */
    char *expanded = (char *) lime_malloc(n + 9); /* "dialect_"+name+NUL */
    memcpy(expanded, "dialect_", 8);
    memcpy(expanded + 8, name, n + 1);
    z = expanded;
  }
  nDefine++;
  azDefine = (char **) lime_realloc(azDefine, sizeof(azDefine[0])*nDefine);
  if( azDefine==0 ){
    fprintf(stderr,"out of memory\n");
    exit(1);
  }
  bDefineUsed = (char*)lime_realloc(bDefineUsed, nDefine);
  if( bDefineUsed==0 ){
    fprintf(stderr,"out of memory\n");
    exit(1);
  }
  bDefineUsed[nDefine-1] = 0;
  paz = &azDefine[nDefine-1];
  *paz = (char *) lime_malloc( lemonStrlen(z)+1 );
  if( *paz==0 ){
    fprintf(stderr,"out of memory\n");
    exit(1);
  }
  lemon_strcpy(*paz, z);
  for(z=*paz; *z && *z!='='; z++){}
  *z = 0;
}

/* This routine is called with the argument to each -U command-line option.
** Omit a previously defined macro.
*/
static void handle_U_option(char *z){
  int i;
  for(i=0; i<nDefine; i++){
    if( strcmp(azDefine[i],z)==0 ){
      nDefine--;
      if( i<nDefine ){
        azDefine[i] = azDefine[nDefine];
        bDefineUsed[i] = bDefineUsed[nDefine];
      }
      break;
    }
  }
}

/* Rember the name of the output directory 
*/
static char *outputDir = NULL;
static void handle_d_option(char *z){
  outputDir = (char *) lime_malloc( lemonStrlen(z)+1 );
  if( outputDir==0 ){
    fprintf(stderr,"out of memory\n");
    exit(1);
  }
  lemon_strcpy(outputDir, z);
}

static char *user_templatename = NULL;
static void handle_T_option(char *z){
  user_templatename = (char *) lime_malloc( lemonStrlen(z)+1 );
  if( user_templatename==0 ){
    memory_error();
  }
  lemon_strcpy(user_templatename, z);
}

static char *prefix_override = NULL;
static void handle_P_option(char *z){
  prefix_override = (char *) lime_malloc( lemonStrlen(z)+1 );
  if( prefix_override==0 ){
    memory_error();
  }
  lemon_strcpy(prefix_override, z);
}

/* Merge together to lists of rules ordered by rule.iRule */
static struct rule *Rule_merge(struct rule *pA, struct rule *pB){
  struct rule *pFirst = 0;
  struct rule **ppPrev = &pFirst;
  while( pA && pB ){
    if( pA->iRule<pB->iRule ){
      *ppPrev = pA;
      ppPrev = &pA->next;
      pA = pA->next;
    }else{
      *ppPrev = pB;
      ppPrev = &pB->next;
      pB = pB->next;
    }
  }
  if( pA ){
    *ppPrev = pA;
  }else{
    *ppPrev = pB;
  }
  return pFirst;
}

/*
** Sort a list of rules in order of increasing iRule value
*/
static struct rule *Rule_sort(struct rule *rp){
  unsigned int i;
  struct rule *pNext;
  struct rule *x[32];
  memset(x, 0, sizeof(x));
  while( rp ){
    pNext = rp->next;
    rp->next = 0;
    for(i=0; i<sizeof(x)/sizeof(x[0])-1 && x[i]; i++){
      rp = Rule_merge(x[i], rp);
      x[i] = 0;
    }
    x[i] = rp;
    rp = pNext;
  }
  rp = 0;
  for(i=0; i<sizeof(x)/sizeof(x[0]); i++){
    rp = Rule_merge(x[i], rp);
  }
  return rp;
}

/* forward reference */
static const char *minimum_size_type(int lwr, int upr, int *pnByte);

/* Print a single line of the "Parser Stats" output
*/
static void stats_line(const char *zLabel, int iValue){
  int nLabel = lemonStrlen(zLabel);
  printf("  %s%.*s %5d\n", zLabel,
         35-nLabel, "................................",
         iValue);
}

/*
** Comparison function used by qsort() to sort the azDefine[] array.
*/
static int defineCmp(const void *pA, const void *pB){
  const char *zA = *(const char**)pA;
  const char *zB = *(const char**)pB;
  return strcmp(zA,zB);
}

/*
** Lint a grammar file - validate directives and check for common errors.
** Returns the number of errors found (0 if no errors).
*/
static int lint_grammar(struct lime *lem){
  int errors = 0;
  int warnings = 0;
  struct symbol *sp;
  struct rule *rp;
  int i;

  printf("Linting %s...\n", lem->filename);

  /* Check module metadata consistency */
  if( lem->module_name && !lem->module_version ){
    fprintf(stderr, "%s:1:1: error: %%module_name requires %%module_version\n",
            lem->filename);
    errors++;
  }

  /* Validate semantic version format */
  if( lem->module_version ){
    int major, minor, patch;
    if( sscanf(lem->module_version, "%d.%d.%d", &major, &minor, &patch) != 3 ){
      fprintf(stderr, "%s:1:1: error: invalid semantic version format: %s\n",
              lem->filename, lem->module_version);
      errors++;
    }
  }

  /* Check that exported symbols are defined */
  if( lem->exports ){
    struct exported_symbol *exp;
    for(exp = lem->exports; exp; exp = exp->next){
      sp = Symbol_find(exp->name);
      if( !sp ){
        fprintf(stderr, "%s:1:1: error: exported symbol '%s' is not defined\n",
                lem->filename, exp->name);
        errors++;
      }else if( sp->type != NONTERMINAL ){
        fprintf(stderr, "%s:1:1: warning: exported symbol '%s' is a terminal (exports are usually non-terminals)\n",
                lem->filename, exp->name);
        warnings++;
      }
    }
  }

  /* Check for undefined symbols in rules - could be expanded in future */
  for(rp = lem->rule; rp; rp = rp->next){
    for(i = 0; i < rp->nrhs; i++){
      sp = rp->rhs[i];
      /* Additional validation could be added here */
      (void)sp; /* Suppress unused warning */
    }
  }

  /* Check for unused non-terminals (have type declaration but no rules) */
  for(i = lem->nterminal; i < lem->nsymbol; i++){
    sp = lem->symbols[i];
    if( sp->type == NONTERMINAL ){
      /* Skip special symbols */
      if( strcmp(sp->name, "error") == 0 ) continue;
      if( strcmp(sp->name, "{default}") == 0 ) continue;

      /* Check if this symbol has any rules that produce it */
      int has_rules = 0;
      for(rp = lem->rule; rp; rp = rp->next){
        if( rp->lhs == sp ){
          has_rules = 1;
          break;
        }
      }

      if( !has_rules && sp->datatype ){
        /* Symbol has a type declaration but no production rules */
        fprintf(stderr, "%s:1:1: warning: non-terminal '%s' has type but no rules\n",
                lem->filename, sp->name);
        warnings++;
      }
    }
  }

  /* Summary */
  printf("\n");
  if( errors == 0 && warnings == 0 ){
    printf("OK: No errors or warnings\n");
  }else{
    fprintf(stderr, "%d errors, %d warnings\n", errors, warnings);
  }

  return errors;
}

/*
** Format a grammar file - rewrite with consistent formatting.
** Returns 0 on success, non-zero on error.
*/
/*
** Skip a leading `#line N "..."\n` directive in a brace-block body.
** The parser injects these into captured bodies (lem->include /
** lem->error / etc.) for codegen line tracking.  When the formatter
** re-emits a body verbatim those markers must NOT survive -- the
** user wrote `%include { #include <stdio.h> ... }` and that's what
** we should round-trip.  Returns a pointer that, when followed by
** the matching close brace, yields the original user content.
**
** Handles both the leading marker and an optional one-blank-line
** prefix (the parser's emit pattern).  If the body has no leading
** #line, returns the body unchanged.  Multiple stacked %include
** directives may produce multiple internal #line markers; this
** strips only the leading one.  Multi-include grammars are an edge
** case; the common single-%include shape round-trips cleanly.
*/
static const char *fmt_skip_linedir(const char *body) {
  if( body==0 ) return 0;
  while( *body=='\n' ) body++;
  if( strncmp(body, "#line ", 6)==0 ){
    const char *nl = strchr(body, '\n');
    if( nl ) body = nl + 1;
  }
  while( *body=='\n' ) body++;
  return body;
}

/*
** Trim the leading newline (\n / \r) the parser captures at the
** start of a brace body and strip ALL trailing whitespace.  The
** emit format is `%directive {\n<body>\n}` -- the parser's body
** typically begins with the newline that followed the opening `{`.
** Without trimming that leading newline, every reformat pass would
** add another blank line at the top of the body, breaking
** idempotence.
**
** Lime-Letter-19: do NOT strip leading spaces or tabs.  Those are
** body indentation that PG's grammar maintainers rely on for
** readability of action bodies and %syntax_error / %parse_failure
** blocks.  The previous incarnation (named fmt_trim_trailing_ws,
** despite trimming both ends) dedented every body, costing PG ~3000
** lines of indentation across gram.lime alone.  Idempotence is
** preserved by the trailing-whitespace trim plus the leading-newline
** trim; preserving leading horizontal whitespace does not drift.
**
** Result is a heap-allocated copy the caller must lime_free; returns
** NULL on OOM.
*/
static char *fmt_trim_body_ws(const char *body) {
  if( body==0 ) return 0;
  /* Strip leading \n / \r so the {\n we emit doesn't compound with
  ** the body's own leading newline (idempotence drift).  Do NOT
  ** strip leading spaces or tabs -- those are body indentation
  ** PG relies on for readability (Lime-Letter-19, loss class B). */
  while( *body=='\n' || *body=='\r' ) body++;
  size_t n = strlen(body);
  while( n>0 && (body[n-1]==' ' || body[n-1]=='\t' || body[n-1]=='\n' ||
                 body[n-1]=='\r') ) n--;
  char *out = (char*)lime_malloc(n + 1);
  if( out==0 ) return 0;
  memcpy(out, body, n);
  out[n] = 0;
  return out;
}

/*
** Emit a brace-delimited directive body for `lime -F` consumption:
** `%name {\n<body>\n}\n\n` with leading #line markers stripped and
** trailing whitespace trimmed.  Used for %include, %syntax_error,
** %parse_failure, %parse_accept, %stack_overflow, %token_destructor,
** and %default_destructor (Lime-Letter-18).
*/
static void fmt_emit_brace_body(FILE *out, const char *directive,
                                const char *body) {
  const char *trimmed = fmt_skip_linedir(body);
  char *clean = fmt_trim_body_ws(trimmed);
  if( clean==0 ){
    fprintf(out, "%%%s {\n%s\n}\n\n", directive, trimmed);
    return;
  }
  fprintf(out, "%%%s {\n%s\n}\n\n", directive, clean);
  lime_free(clean);
}

static int format_grammar(struct lime *lem){
  FILE *out;
  struct rule *rp;
  struct symbol *sp;
  int i;
  char *outfile;
  int sz;

  printf("Formatting %s...\n", lem->filename);

  /* Create output filename (input.lime -> input.lime.formatted) */
  sz = lemonStrlen(lem->filename) + 20;
  outfile = (char*)lime_malloc(sz);
  lemon_sprintf(outfile, "%s.formatted", lem->filename);

  out = fopen(outfile, "w");
  if( !out ){
    fprintf(stderr, "Cannot open %s for writing\n", outfile);
    lime_free(outfile);
    return 1;
  }

  /* Header comment (Lime-Letter-18 -- preserve copyright /
  ** IDENTIFICATION / module-doc verbatim).  If captured, emit it
  ** as-is followed by a single blank line; if not captured (file
  ** opens with a directive at byte 0), fall back to the
  ** boilerplate banner so the formatter remains visibly idempotent.
  */
  if( lem->header_comment ){
    fprintf(out, "%s\n\n", lem->header_comment);
  }else{
    fprintf(out, "/* Formatted by Lime */\n\n");
  }

  /* Output module directives */
  if( lem->module_name ){
    if( lem->module_comment ) fprintf(out, "%s\n", lem->module_comment);
    fprintf(out, "%%module_name %s\n", lem->module_name);
    if( lem->module_version ){
      fprintf(out, "%%module_version \"%s\"\n", lem->module_version);
    }
    if( lem->module_description ){
      fprintf(out, "%%module_description \"%s\"\n", lem->module_description);
    }
    fprintf(out, "\n");
  }

  /* Output dependencies */
  if( lem->dependencies ){
    struct module_dependency *dep;
    for(dep = lem->dependencies; dep; dep = dep->next){
      fprintf(out, "%%require %s", dep->name);
      if( dep->version_constraint ){
        fprintf(out, " \"%s\"", dep->version_constraint);
      }
      fprintf(out, ".\n");
    }
    fprintf(out, "\n");
  }

  /* Output exports */
  if( lem->exports ){
    struct exported_symbol *exp;
    fprintf(out, "%%export");
    for(exp = lem->exports; exp; exp = exp->next){
      fprintf(out, " %s", exp->name);
    }
    fprintf(out, ".\n\n");
  }

  /* Output imports */
  if( lem->imports ){
    struct imported_symbol *imp;
    char *last_module = NULL;

    /* Group imports by source module */
    for(imp = lem->imports; imp; imp = imp->next){
      if( !last_module || strcmp(last_module, imp->from_module) != 0 ){
        if( last_module ){
          fprintf(out, ".\n");
        }
        fprintf(out, "%%import");
        last_module = imp->from_module;
      }
      fprintf(out, " %s", imp->name);
    }
    if( last_module ){
      fprintf(out, " from %s.\n\n", last_module);
    }
  }

  /* Output standard Lime directives */
  if( lem->name ){
    if( lem->name_comment ) fprintf(out, "%s\n", lem->name_comment);
    fprintf(out, "%%name %s\n", lem->name);
  }
  if( lem->tokentype ){
    if( lem->tokentype_comment ) fprintf(out, "%s\n", lem->tokentype_comment);
    fprintf(out, "%%token_type {%s}\n", lem->tokentype);
  }
  if( lem->arg ){
    if( lem->arg_comment ) fprintf(out, "%s\n", lem->arg_comment);
    fprintf(out, "%%extra_argument {%s}\n", lem->arg);
  }
  if( lem->ctx ){
    if( lem->ctx_comment ) fprintf(out, "%s\n", lem->ctx_comment);
    fprintf(out, "%%extra_context {%s}\n", lem->ctx);
  }
  if( lem->vartype ){
    if( lem->vartype_comment ) fprintf(out, "%s\n", lem->vartype_comment);
    fprintf(out, "%%default_type {%s}\n", lem->vartype);
  }
  if( lem->start ){
    if( lem->start_comment ) fprintf(out, "%s\n", lem->start_comment);
    fprintf(out, "%%start_symbol %s\n", lem->start);
  }
  if( lem->stacksize ){
    if( lem->stacksize_comment ) fprintf(out, "%s\n", lem->stacksize_comment);
    fprintf(out, "%%stack_size %s\n", lem->stacksize);
  }
  if( lem->tokenprefix ){
    if( lem->tokenprefix_comment ) fprintf(out, "%s\n", lem->tokenprefix_comment);
    fprintf(out, "%%token_prefix %s\n", lem->tokenprefix);
  }
  if( lem->symbolprefix ){
    if( lem->symbolprefix_comment ) fprintf(out, "%s\n", lem->symbolprefix_comment);
    fprintf(out, "%%symbol_prefix %s\n", lem->symbolprefix);
  }
  if( lem->nexpect >= 0 ){
    if( lem->nexpect_comment ) fprintf(out, "%s\n", lem->nexpect_comment);
    fprintf(out, "%%expect %d\n", lem->nexpect);
  }
  fprintf(out, "\n");

  /* Brace-delimited directive bodies (Lime-Letter-18 -- preserve
  ** verbatim; the formatter has no business reformatting C code
  ** embedded in a brace block).  Each is emitted only when the
  ** parser captured it.  Lime-Letter-19 adds the matching
  ** _comment slot (when present) immediately above the directive. */
  if( lem->include    ){
    if( lem->include_comment ) fprintf(out, "%s\n", lem->include_comment);
    fmt_emit_brace_body(out, "include",            lem->include);
  }
  if( lem->error      ){
    if( lem->error_comment   ) fprintf(out, "%s\n", lem->error_comment);
    fmt_emit_brace_body(out, "syntax_error",       lem->error);
  }
  if( lem->failure    ){
    if( lem->failure_comment ) fprintf(out, "%s\n", lem->failure_comment);
    fmt_emit_brace_body(out, "parse_failure",      lem->failure);
  }
  if( lem->accept     ){
    if( lem->accept_comment  ) fprintf(out, "%s\n", lem->accept_comment);
    fmt_emit_brace_body(out, "parse_accept",       lem->accept);
  }
  if( lem->overflow   ){
    if( lem->overflow_comment ) fprintf(out, "%s\n", lem->overflow_comment);
    fmt_emit_brace_body(out, "stack_overflow",     lem->overflow);
  }
  if( lem->tokendest  ){
    if( lem->tokendest_comment ) fprintf(out, "%s\n", lem->tokendest_comment);
    fmt_emit_brace_body(out, "token_destructor",   lem->tokendest);
  }
  if( lem->vardest    ){
    if( lem->vardest_comment ) fprintf(out, "%s\n", lem->vardest_comment);
    fmt_emit_brace_body(out, "default_destructor", lem->vardest);
  }

  /* Output token declarations.  Lime-Letter-21: emit %token
  ** sections in declaration order, one group at a time, with
  ** each group's leading comment (the section banner above it
  ** in source) preserved verbatim.  Inter-symbol comments INSIDE
  ** a group are dropped per the PG-team-accepted v0.3.5 cheap
  ** variant -- section banners between groups are what PG's
  ** gram.lime actually documents (12 banners + 5 type banners,
  ** ~120-150 lines on the `lime -F` round-trip diff before
  ** this fix).  Falls back to symbol-index order when the
  ** grammar declared no %token directives (terminals introduced
  ** purely on RHS) so the formatter still emits a usable file. */
  if( lem->first_token_group ){
    LimeTokenGroup *g;
    /* Mark which terminals appear in some %token group so any
    ** that don't (introduced only on RHS, or via %type without
    ** an accompanying %token) can be emitted at the tail of the
    ** section.  Without this trailing pass an undeclared-but-
    ** used terminal would be silently lost on round-trip. */
    char *emitted = (char *)lime_calloc((size_t)lem->nsymbol, 1);
    for( g = lem->first_token_group; g; g = g->next ){
      if( g->leading_comment ){
        fprintf(out, "%s\n", g->leading_comment);
      }
      for( i = 0; i < g->n_symbols; i++ ){
        sp = g->symbols[i];
        if( sp->type == TERMINAL && strcmp(sp->name, "$") != 0 ){
          fprintf(out, "%%token %s", sp->name);
          if( sp->datatype ){
            fprintf(out, " {%s}", sp->datatype);
          }
          fprintf(out, ".\n");
        }
        if( emitted && sp->index >= 0 && sp->index < lem->nsymbol ){
          emitted[sp->index] = 1;
        }
      }
    }
    if( emitted ){
      for( i = 1; i < lem->nterminal; i++ ){
        sp = lem->symbols[i];
        if( !emitted[i] && sp->type == TERMINAL
            && strcmp(sp->name, "$") != 0 ){
          fprintf(out, "%%token %s", sp->name);
          if( sp->datatype ){
            fprintf(out, " {%s}", sp->datatype);
          }
          fprintf(out, ".\n");
        }
      }
      lime_free(emitted);
    }
  }else{
    for(i = 1; i < lem->nterminal; i++){
      sp = lem->symbols[i];
      if( sp->type == TERMINAL && strcmp(sp->name, "$") != 0 ){
        fprintf(out, "%%token %s", sp->name);
        if( sp->datatype ){
          fprintf(out, " {%s}", sp->datatype);
        }
        fprintf(out, ".\n");
      }
    }
  }
  fprintf(out, "\n");

  /* Output type declarations.  Same group-aware policy as %token
  ** above.  A %type group's symbols can include terminals (when
  ** PG writes `%type ICONST {int}` to attach a datatype to a
  ** token); those are skipped here because the %token loop above
  ** already emitted them with the {datatype} appended.  This
  ** matches the pre-Letter-21 index-order behaviour exactly. */
  if( lem->first_type_group ){
    LimeTypeGroup *g;
    char *emitted = (char *)lime_calloc((size_t)lem->nsymbol, 1);
    for( g = lem->first_type_group; g; g = g->next ){
      if( g->leading_comment ){
        fprintf(out, "%s\n", g->leading_comment);
      }
      for( i = 0; i < g->n_symbols; i++ ){
        sp = g->symbols[i];
        if( sp->type != TERMINAL && sp->datatype ){
          fprintf(out, "%%type %s {%s}\n", sp->name, sp->datatype);
        }
        if( emitted && sp->index >= 0 && sp->index < lem->nsymbol ){
          emitted[sp->index] = 1;
        }
      }
    }
    if( emitted ){
      for( i = lem->nterminal; i < lem->nsymbol; i++ ){
        sp = lem->symbols[i];
        if( !emitted[i] && sp->datatype ){
          fprintf(out, "%%type %s {%s}\n", sp->name, sp->datatype);
        }
      }
      lime_free(emitted);
    }
  }else{
    for(i = lem->nterminal; i < lem->nsymbol; i++){
      sp = lem->symbols[i];
      if( sp->datatype ){
        fprintf(out, "%%type %s {%s}\n", sp->name, sp->datatype);
      }
    }
  }
  fprintf(out, "\n");

  /* Output precedence directives -- the missing piece that exploded
  ** PG's canonicalize-the-source pass into 1682 spurious conflicts.
  ** Group symbols by their (prec, assoc) tuple; each tuple was one
  ** source `%left/%right/%nonassoc` directive (preccounter increments
  ** per directive line in parseonetoken).  Symbols without explicit
  ** precedence have prec == -1 and are skipped.
  **
  ** Lemon assigns precedence by ORDER of these directives -- a later
  ** %left binds tighter than an earlier one -- so the round-trip
  ** must emit them in ascending prec.  Without this block, every
  ** binary operator becomes ambiguous on shift/reduce conflicts and
  ** the formatted grammar bears no semantic resemblance to the
  ** source.  Latent since v0.3.0; surfaced by Letter-22's full
  ** PG-grammar canonicalize pass.
  **
  ** Two passes: first determine max_prec, then emit each prec level.
  ** Within a prec level, all symbols share a single (prec, assoc)
  ** tuple by construction (Lemon won't accept differing assoc on the
  ** same line).  We use the first-seen symbol's assoc as authoritative. */
  {
    int max_prec = -1;
    int j;
    for(j = 0; j < lem->nsymbol; j++){
      if( lem->symbols[j]->prec > max_prec ){
        max_prec = lem->symbols[j]->prec;
      }
    }
    int p;
    for(p = 0; p <= max_prec; p++){
      enum e_assoc assoc = NONE;
      int found = 0;
      for(j = 0; j < lem->nsymbol; j++){
        struct symbol *sp = lem->symbols[j];
        if( sp->prec != p ) continue;
        if( !found ){
          assoc = sp->assoc;
          found = 1;
          const char *kw = (assoc == LEFT)  ? "left"
                         : (assoc == RIGHT) ? "right"
                                            : "nonassoc";
          fprintf(out, "%%%s", kw);
        }
        fprintf(out, " %s", sp->name);
      }
      if( found ) fprintf(out, ".\n");
    }
    if( max_prec >= 0 ) fprintf(out, "\n");
  }

  /* Output grammar rules.  Banner dropped: it would otherwise be
  ** captured as the first rule's leading_comment on a reformat pass. */
  for(rp = lem->rule; rp; rp = rp->next){
    /* Lime-Letter-19: per-rule leading comment captured by the
    ** parser at the moment we recognized this rule's LHS NT.
    ** For rules that arrived via `|` alternation, only the first
    ** rule of the group has a leading_comment; the others emit
    ** without one. */
    if( rp->leading_comment ) fprintf(out, "%s\n", rp->leading_comment);
    fprintf(out, "%s", rp->lhs->name);
    /* Lime-Letter-20: emit LHS / RHS letter labels.  These are the
    ** stack-slot aliases that action bodies reference (the Lime
    ** equivalent of Bison's $$ / $N).  Stripping them at format time
    ** while keeping action-body references produces a grammar that
    ** lints clean but breaks at generated-.c compile time.  Letter 20
    ** PG team reproducer: contrib/cube/cubeparse.lime had every
    ** (A) / (B) / (C) stripped and gcc -c failed on the action
    ** bodies' undeclared identifiers. */
    if( rp->lhsalias ) fprintf(out, "(%s)", rp->lhsalias);
    fprintf(out, " ::=");
    for(i = 0; i < rp->nrhs; i++){
      fprintf(out, " %s", rp->rhs[i]->name);
      if( rp->rhsalias && rp->rhsalias[i] ){
        fprintf(out, "(%s)", rp->rhsalias[i]);
      }
    }
    fprintf(out, ".");
    /* Lime-Letter-20 follow-on: emit explicit [SYMBOL] precedence
    ** marker.  format_grammar() runs before FindRulePrecedences()
    ** in main() (see lime.c:2535 vs 2561), so rp->precsym is
    ** non-NULL only for rules that carried an explicit [X] marker
    ** in source.  Emitting unconditionally would over-emit for
    ** rules whose precedence is implicit (inherited from the first
    ** precedence-bearing RHS terminal); this guard preserves the
    ** source's intent. */
    if( rp->precsym ) fprintf(out, " [%s]", rp->precsym->name);

    if( rp->code ){
      /* Action bodies, like %include, get a leading #line marker
      ** prepended by the parser for codegen line tracking
      ** (Lime-Letter-18).  Strip it before emitting and trim trailing
      ** whitespace so the formatter is idempotent: format(format(F))
      ** == format(F).  Do NOT re-indent the body -- whatever the
      ** user wrote inside the braces is opaque to us.  Lime-Letter-19:
      ** also do NOT dedent leading horizontal whitespace -- only the
      ** leading newline (the byte the parser captured immediately
      ** after the opening `{`) is stripped, so PG's tab-indented
      ** action bodies (\tcompute_plus(...);) survive intact.
      **
      ** The emit shape is multi-line `. {\n<body>\n}`.  v0.3.1 used
      ** the single-line wrap `. { %s }`, but with the new (correct)
      ** preserve-leading-WS trim that drifts on every reformat pass
      ** (the format string's own leading space accumulates with the
      ** body's preserved leading space).  The multi-line shape is
      ** strictly idempotent and matches PG's source convention. */
      const char *code = fmt_skip_linedir(rp->code);
      char *clean = fmt_trim_body_ws(code);
      const char *body = clean ? clean : code;
      fprintf(out, " {\n%s\n}", body);
      lime_free(clean);
    }
    fprintf(out, "\n");
  }

  /* Lime-Letter-19: trailing comments captured between the last
  ** rule's terminator and EOF (e.g. a closing modeline or banner).
  ** Emitted with one blank line of separation so the formatter's
  ** rule-loop \n + this directive's \n leave a single blank line
  ** above the trailing block, matching the source convention. */
  if( lem->trailing_comment ){
    fprintf(out, "\n%s\n", lem->trailing_comment);
  }

  fclose(out);
  printf("OK: Formatted output written to: %s\n", outfile);
  printf("  Review the formatted file and rename it if correct:\n");
  printf("    mv %s %s\n", outfile, lem->filename);

  /* Lime-Letter-21: free the per-kind group lists.  Symbol pointers
  ** inside groups are aliases to the global symbol table (owned
  ** elsewhere); we only own the group records, the leading_comment
  ** strings, and the symbols[] arrays. */
  {
    LimeTokenGroup *g, *next;
    for( g = lem->first_token_group; g; g = next ){
      next = g->next;
      if( g->leading_comment ) lime_free(g->leading_comment);
      if( g->symbols ) lime_free(g->symbols);
      lime_free(g);
    }
    lem->first_token_group = lem->last_token_group = 0;
  }
  {
    LimeTypeGroup *g, *next;
    for( g = lem->first_type_group; g; g = next ){
      next = g->next;
      if( g->leading_comment ) lime_free(g->leading_comment);
      if( g->symbols ) lime_free(g->symbols);
      lime_free(g);
    }
    lem->first_type_group = lem->last_type_group = 0;
  }

  lime_free(outfile);
  return 0;
}

/* The main program.  Parse the command line and do it... */
int main(int argc, char **argv){
  static int version = 0;
  static int rpflag = 0;
  static int basisflag = 0;
  static int compress = 0;
  static int quiet = 0;
  static int statistics = 0;
  static int mhflag = 0;
  static int nolinenosflag = 0;
  static int noResort = 0;
  static int sqlFlag = 0;
  static int printPP = 0;
  static int snapshotFlag = 0;
  static int lintFlag = 0;
  static int formatFlag = 0;
  static int verboseConflict = 0;
  static int aotFlag = 0;
  static int lexFlag = 0;       /* -X: run as .lex compiler */

  /* Forward decl: defined in src/lex/lex_main.c.  When lime is built
  ** as the single-file `cc -o lime lime.c` (no library link), the
  ** weak fallback at the top of this file stands in for the lex
  ** compiler so the link succeeds and `-X` reports a clear error
  ** rather than failing at link time.  When meson links
  ** lime_lex_compiler_lib, the strong definition in lex_main.c wins
  ** and `-X` works normally. */
  extern int lime_lex_run_compiler(const char *input_path,
                                   const char *output_dir);


  static struct s_options options[] = {
    {OPT_FLAG, "b", (char*)&basisflag, "Print only the basis in report."},
    {OPT_FLAG, "c", (char*)&compress, "Don't compress the action table."},
    {OPT_FSTR, "d", (char*)&handle_d_option, "Output directory.  Default '.'"},
    {OPT_FSTR, "D", (char*)handle_D_option, "Define an %ifdef macro."},
    {OPT_FLAG, "E", (char*)&printPP, "Print input file after preprocessing."},
    {OPT_FSTR, "f", 0, "Ignored.  (Placeholder for -f compiler options.)"},
    {OPT_FLAG, "F", (char*)&formatFlag, "Format grammar file consistently."},
    {OPT_FLAG, "g", (char*)&rpflag, "Print grammar without actions."},
    {OPT_FLAG, "j", (char*)&aotFlag,
                    "Generate AOT-compiled action table (*_aot.c)."},
    {OPT_FSTR, "I", 0, "Ignored.  (Placeholder for '-I' compiler options.)"},
    {OPT_FLAG, "L", (char*)&lintFlag, "Validate grammar and module directives."},
    {OPT_FLAG, "m", (char*)&mhflag, "Output a makeheaders compatible file."},
    {OPT_FLAG, "l", (char*)&nolinenosflag, "Do not print #line statements."},
    {OPT_FLAG, "n", (char*)&snapshotFlag,
                    "Generate snapshot init code (*_snapshot.c)."},
    {OPT_FSTR, "O", 0, "Ignored.  (Placeholder for '-O' compiler options.)"},
    {OPT_FLAG, "p", (char*)&showPrecedenceConflict,
                    "Show conflicts resolved by precedence rules"},
    {OPT_FSTR, "P", (char*)handle_P_option,
                    "Prefix for generated parser symbols (overrides %name)."},
    {OPT_FLAG, "q", (char*)&quiet, "(Quiet) Don't print the report file."},
    {OPT_FLAG, "r", (char*)&noResort, "Do not sort or renumber states"},
    {OPT_FLAG, "s", (char*)&statistics,
                                   "Print parser stats to standard output."},
    {OPT_FLAG, "S", (char*)&sqlFlag,
                    "Generate the *.sql file describing the parser tables."},
    {OPT_FLAG, "x", (char*)&version, "Print the version number."},
    {OPT_FLAG, "v", (char*)&version, "Print the version number (alias of -x)."},
    {OPT_FSTR, "T", (char*)handle_T_option, "Specify a template file."},
    {OPT_FSTR, "U", (char*)handle_U_option, "Undefine a macro."},
    {OPT_FLAG, "V", (char*)&verboseConflict,
                    "Verbose conflict diagnostics with derivation paths."},
    {OPT_FLAG, "X", (char*)&lexFlag,
                    "Run as .lex compiler (lexer subsystem M1 frontend)."},
    {OPT_FSTR, "W", 0, "Ignored.  (Placeholder for '-W' compiler options.)"},
    {OPT_FLAG,0,0,0}
  };
  int i;
  int exitcode;
  struct lime lem;
  struct rule *rp;

  OptInit(argv,options,stderr);
  if( version ){
     printf("lime %s\n", LIME_VERSION_STRING);
     exit(0);
  }
  if( OptNArgs()!=1 ){
    fprintf(stderr,"Exactly one filename argument is required.\n");
    exit(1);
  }
  if( lexFlag ){
    /* -X: run the .lex compiler frontend instead of the parser
    ** generator.  Reads the input as a .lex source file, runs
    ** parse -> resolve -> pretty-print, writes canonical .lex
    ** to stdout.  M2 will extend this to emit DFA tables and
    ** generated C runtime code. */
    const char *input = NULL;
    int k;
    for(k=1; argv[k]; k++){
      if( argv[k][0]=='-' || strchr(argv[k],'=') ) continue;
      input = argv[k];
      break;
    }
    if( !input ){
      fprintf(stderr, "lime -X: missing input filename\n");
      exit(1);
    }
    /* outputDir is the static set by handle_d_option; pass it
    ** through so the .lex compiler emits .c/.h to the same place
    ** the parser generator would. */
    exit(lime_lex_run_compiler(input, outputDir));
  }
  memset(&lem, 0, sizeof(lem));
  lem.errorcnt = 0;
  lem.nexpect = -1;
  lem.first_token = 0;
  /* qsort(NULL, 0, ...) is undefined per POSIX (the first argument is
  ** documented as "never null").  Skip the sort when the array is
  ** empty -- caught by UBSan. */
  if( nDefine > 0 ){
    qsort(azDefine, nDefine, sizeof(azDefine[0]), defineCmp);
  }

  /* Initialize the machine */
  Strsafe_init();
  Symbol_init();
  State_init();
  lem.argv = argv;
  lem.argc = argc;
  lem.filename = OptArg(0);
  lem.basisflag = basisflag;
  lem.nolinenosflag = nolinenosflag;
  lem.printPreprocessed = printPP;
  Symbol_new("$");

  /* Parse the input file */
  Parse(&lem);
  if( lem.printPreprocessed || lem.errorcnt ) exit(lem.errorcnt);

  /* Command-line -P overrides %name from the grammar file. Useful for
  ** linking multiple generated parsers without symbol collisions. */
  if( prefix_override ){
    lem.name = prefix_override;
  }

  if( lem.nrule==0 ){
    fprintf(stderr,"Empty grammar.\n");
    exit(1);
  }
  lem.errsym = Symbol_find("error");

  /* Count and index the symbols of the grammar */
  Symbol_new("{default}");
  lem.nsymbol = Symbol_count();
  lem.symbols = Symbol_arrayof();
  for(i=0; i<lem.nsymbol; i++) lem.symbols[i]->index = i;
  qsort(lem.symbols,lem.nsymbol,sizeof(struct symbol*), Symbolcmpp);
  for(i=0; i<lem.nsymbol; i++) lem.symbols[i]->index = i;
  while( lem.symbols[i-1]->type==MULTITERMINAL ){ i--; }
  assert( strcmp(lem.symbols[i-1]->name,"{default}")==0 );
  lem.nsymbol = i - 1;
  for(i=1; ISUPPER(lem.symbols[i]->name[0]); i++);
  lem.nterminal = i;

  /* Handle --lint flag (needs symbols to be counted) */
  if( lintFlag ){
    int lint_errors = lint_grammar(&lem);
    exit(lint_errors);
  }

  /* Handle -F flag (needs symbols to be counted) */
  if( formatFlag ){
    int format_result = format_grammar(&lem);
    exit(format_result);
  }

  /* Assign sequential rule numbers.  Start with 0.  Put rules that have no
  ** reduce action C-code associated with them last, so that the switch()
  ** statement that selects reduction actions will have a smaller jump table.
  */
  for(i=0, rp=lem.rule; rp; rp=rp->next){
    rp->iRule = rp->code ? i++ : -1;
  }
  lem.nruleWithAction = i;
  for(rp=lem.rule; rp; rp=rp->next){
    if( rp->iRule<0 ) rp->iRule = i++;
  }
  lem.startRule = lem.rule;
  lem.rule = Rule_sort(lem.rule);

  /* Generate a reprint of the grammar, if requested on the command line */
  if( rpflag ){
    Reprint(&lem);
  }else{
    /* Initialize the size for all follow and first sets */
    SetSize(lem.nterminal+1);

    /* Find the precedence for every production rule (that has one) */
    FindRulePrecedences(&lem);

    /* Compute the lambda-nonterminals and the first-sets for every
    ** nonterminal */
    FindFirstSets(&lem);

    /* Compute all LR(0) states.  Also record follow-set propagation
    ** links so that the follow-set can be computed later */
    lem.nstate = 0;
    FindStates(&lem);
    lem.sorted = State_arrayof();

    /* Tie up loose ends on the propagation links */
    FindLinks(&lem);

    /* Compute the follow set of every reducible configuration */
    FindFollowSets(&lem);

    /* Compute the action tables */
    FindActions(&lem);

    /* Compress the action tables */
    if( compress==0 ) CompressTables(&lem);

    /* Reorder and renumber the states so that states with fewer choices
    ** occur at the end.  This is an optimization that helps make the
    ** generated parser tables smaller. */
    if( noResort==0 ) ResortStates(&lem);

    /* Generate a report of the parser generated.  (the "y.output" file) */
    if( !quiet ) ReportOutput(&lem);

    /* Generate automatic AST reduction actions if %ast_auto is enabled */
    GenerateASTActions(&lem);

    /* Generate the source code for the parser */
    ReportTable(&lem, mhflag, sqlFlag);

    /* Generate snapshot initialization code if -n flag is set */
    if( snapshotFlag ) ReportSnapshotInit(&lem);

    /* Generate AOT-compiled action table if -j flag is set */
    if( aotFlag ) ReportAOTTable(&lem);

    /* Produce a header file for use by the scanner.  (This step is
    ** omitted if the "-m" option is used because makeheaders will
    ** generate the file for us.) */
    if( !mhflag ) ReportHeader(&lem);
  }
  if( statistics ){
    printf("Parser statistics:\n");
    stats_line("terminal symbols", lem.nterminal);
    stats_line("non-terminal symbols", lem.nsymbol - lem.nterminal);
    stats_line("total symbols", lem.nsymbol);
    stats_line("rules", lem.nrule);
    stats_line("states", lem.nxstate);
    stats_line("conflicts", lem.nconflict);
    stats_line("action table entries", lem.nactiontab);
    stats_line("lookahead table entries", lem.nlookaheadtab);
    stats_line("total table size (bytes)", lem.tablesize);
  }
  if( lem.nconflict > 0 ){
    fprintf(stderr,"%d parsing conflicts.\n",lem.nconflict);
  }

  /* return 0 on success, 1 on failure.
  ** If %expect N is set, only fail when the actual conflict count
  ** differs from the expected count.  This matches Bison's behavior. */
  if( lem.nexpect >= 0 ){
    if( lem.nconflict != lem.nexpect ){
      fprintf(stderr,"Expected %d conflict(s) but found %d.\n",
              lem.nexpect, lem.nconflict);
      exitcode = 1;
    }else{
      exitcode = (lem.errorcnt > 0) ? 1 : 0;
    }
  }else{
    exitcode = ((lem.errorcnt > 0) || (lem.nconflict > 0)) ? 1 : 0;
  }
  lime_free_all();
  exit(exitcode);
  return (exitcode);
}
/******************** From the file "msort.c" *******************************/
/*
** A generic merge-sort program.
**
** USAGE:
** Let "ptr" be a pointer to some structure which is at the head of
** a null-terminated list.  Then to sort the list call:
**
**     ptr = msort(ptr,&(ptr->next),cmpfnc);
**
** In the above, "cmpfnc" is a pointer to a function which compares
** two instances of the structure and returns an integer, as in
** strcmp.  The second argument is a pointer to the pointer to the
** second element of the linked list.  This address is used to compute
** the offset to the "next" field within the structure.  The offset to
** the "next" field must be constant for all structures in the list.
**
** The function returns a new pointer which is the head of the list
** after sorting.
**
** ALGORITHM:
** Merge-sort.
*/

/*
** Return a pointer to the next structure in the linked list.
*/
#define NEXT(A) (*(char**)(((char*)A)+offset))

/*
** Inputs:
**   a:       A sorted, null-terminated linked list.  (May be null).
**   b:       A sorted, null-terminated linked list.  (May be null).
**   cmp:     A pointer to the comparison function.
**   offset:  Offset in the structure to the "next" field.
**
** Return Value:
**   A pointer to the head of a sorted list containing the elements
**   of both a and b.
**
** Side effects:
**   The "next" pointers for elements in the lists a and b are
**   changed.
*/
static char *merge(
  char *a,
  char *b,
  int (*cmp)(const char*,const char*),
  int offset
){
  char *ptr, *head;

  if( a==0 ){
    head = b;
  }else if( b==0 ){
    head = a;
  }else{
    if( (*cmp)(a,b)<=0 ){
      ptr = a;
      a = NEXT(a);
    }else{
      ptr = b;
      b = NEXT(b);
    }
    head = ptr;
    while( a && b ){
      if( (*cmp)(a,b)<=0 ){
        NEXT(ptr) = a;
        ptr = a;
        a = NEXT(a);
      }else{
        NEXT(ptr) = b;
        ptr = b;
        b = NEXT(b);
      }
    }
    if( a ) NEXT(ptr) = a;
    else    NEXT(ptr) = b;
  }
  return head;
}

/*
** Inputs:
**   list:      Pointer to a singly-linked list of structures.
**   next:      Pointer to pointer to the second element of the list.
**   cmp:       A comparison function.
**
** Return Value:
**   A pointer to the head of a sorted list containing the elements
**   originally in list.
**
** Side effects:
**   The "next" pointers for elements in list are changed.
*/
#define LISTSIZE 30
static char *msort(
  char *list,
  char **next,
  int (*cmp)(const char*,const char*)
){
  unsigned long offset;
  char *ep;
  char *set[LISTSIZE];
  int i;
  offset = (unsigned long)((char*)next - (char*)list);
  for(i=0; i<LISTSIZE; i++) set[i] = 0;
  while( list ){
    ep = list;
    list = NEXT(list);
    NEXT(ep) = 0;
    for(i=0; i<LISTSIZE-1 && set[i]!=0; i++){
      ep = merge(set[i],ep,cmp,offset);
      set[i] = 0;
    }
    set[i] = merge(set[i],ep,cmp,offset);
  }
  ep = 0;
  for(i=0; i<LISTSIZE; i++) if( set[i] ) ep = merge(set[i],ep,cmp,offset);
  return ep;
}
/************************ From the file "option.c" **************************/
static char **g_argv;
static struct s_options *op;
static FILE *errstream;

#define ISOPT(X) ((X)[0]=='-'||(X)[0]=='+'||strchr((X),'=')!=0)

/*
** Print the command line with a carrot pointing to the k-th character
** of the n-th field.
*/
static void errline(int n, int k, FILE *err)
{
  int spcnt, i;
  if( g_argv[0] ){
    fprintf(err,"%s",g_argv[0]);
    spcnt = lemonStrlen(g_argv[0]) + 1;
  }else{
    spcnt = 0;
  }
  for(i=1; i<n && g_argv[i]; i++){
    fprintf(err," %s",g_argv[i]);
    spcnt += lemonStrlen(g_argv[i])+1;
  }
  spcnt += k;
  for(; g_argv[i]; i++) fprintf(err," %s",g_argv[i]);
  if( spcnt<20 ){
    fprintf(err,"\n%*s^-- here\n",spcnt,"");
  }else{
    fprintf(err,"\n%*shere --^\n",spcnt-7,"");
  }
}

/*
** Return the index of the N-th non-switch argument.  Return -1
** if N is out of range.
*/
static int argindex(int n)
{
  int i;
  int dashdash = 0;
  if( g_argv!=0 && *g_argv!=0 ){
    for(i=1; g_argv[i]; i++){
      if( dashdash || !ISOPT(g_argv[i]) ){
        if( n==0 ) return i;
        n--;
      }
      if( strcmp(g_argv[i],"--")==0 ) dashdash = 1;
    }
  }
  return -1;
}

static char emsg[] = "Command line syntax error: ";

/*
** Process a flag command line argument.
*/
static int handleflags(int i, FILE *err)
{
  int v;
  int errcnt = 0;
  int j;
  for(j=0; op[j].label; j++){
    if( strncmp(&g_argv[i][1],op[j].label,lemonStrlen(op[j].label))==0 ) break;
  }
  v = g_argv[i][0]=='-' ? 1 : 0;
  if( op[j].label==0 ){
    if( err ){
      fprintf(err,"%sundefined option.\n",emsg);
      errline(i,1,err);
    }
    errcnt++;
  }else if( op[j].arg==0 ){
    /* Ignore this option */
  }else if( op[j].type==OPT_FLAG ){
    *((int*)op[j].arg) = v;
  }else if( op[j].type==OPT_FFLAG ){
    (*(void(*)(int))(op[j].arg))(v);
  }else if( op[j].type==OPT_FSTR ){
    (*(void(*)(char *))(op[j].arg))(&g_argv[i][2]);
  }else{
    if( err ){
      fprintf(err,"%smissing argument on switch.\n",emsg);
      errline(i,1,err);
    }
    errcnt++;
  }
  return errcnt;
}

/*
** Process a command line switch which has an argument.
*/
static int handleswitch(int i, FILE *err)
{
  int lv = 0;
  double dv = 0.0;
  char *sv = 0, *end;
  char *cp;
  int j;
  int errcnt = 0;
  cp = strchr(g_argv[i],'=');
  assert( cp!=0 );
  *cp = 0;
  for(j=0; op[j].label; j++){
    if( strcmp(g_argv[i],op[j].label)==0 ) break;
  }
  *cp = '=';
  if( op[j].label==0 ){
    if( err ){
      fprintf(err,"%sundefined option.\n",emsg);
      errline(i,0,err);
    }
    errcnt++;
  }else{
    cp++;
    switch( op[j].type ){
      case OPT_FLAG:
      case OPT_FFLAG:
        if( err ){
          fprintf(err,"%soption requires an argument.\n",emsg);
          errline(i,0,err);
        }
        errcnt++;
        break;
      case OPT_DBL:
      case OPT_FDBL:
        dv = strtod(cp,&end);
        if( *end ){
          if( err ){
            fprintf(err,
               "%sillegal character in floating-point argument.\n",emsg);
            errline(i,(int)((char*)end-(char*)g_argv[i]),err);
          }
          errcnt++;
        }
        break;
      case OPT_INT:
      case OPT_FINT:
        lv = strtol(cp,&end,0);
        if( *end ){
          if( err ){
            fprintf(err,"%sillegal character in integer argument.\n",emsg);
            errline(i,(int)((char*)end-(char*)g_argv[i]),err);
          }
          errcnt++;
        }
        break;
      case OPT_STR:
      case OPT_FSTR:
        sv = cp;
        break;
    }
    switch( op[j].type ){
      case OPT_FLAG:
      case OPT_FFLAG:
        break;
      case OPT_DBL:
        *(double*)(op[j].arg) = dv;
        break;
      case OPT_FDBL:
        (*(void(*)(double))(op[j].arg))(dv);
        break;
      case OPT_INT:
        *(int*)(op[j].arg) = lv;
        break;
      case OPT_FINT:
        (*(void(*)(int))(op[j].arg))((int)lv);
        break;
      case OPT_STR:
        *(char**)(op[j].arg) = sv;
        break;
      case OPT_FSTR:
        (*(void(*)(char *))(op[j].arg))(sv);
        break;
    }
  }
  return errcnt;
}

int OptInit(char **a, struct s_options *o, FILE *err)
{
  int errcnt = 0;
  g_argv = a;
  op = o;
  errstream = err;
  if( g_argv && *g_argv && op ){
    int i;
    for(i=1; g_argv[i]; i++){
      if( g_argv[i][0]=='+' || g_argv[i][0]=='-' ){
        errcnt += handleflags(i,err);
      }else if( strchr(g_argv[i],'=') ){
        errcnt += handleswitch(i,err);
      }
    }
  }
  if( errcnt>0 ){
    fprintf(err,"Valid command line options for \"%s\" are:\n",*a);
    OptPrint();
    exit(1);
  }
  return 0;
}

int OptNArgs(void){
  int cnt = 0;
  int dashdash = 0;
  int i;
  if( g_argv!=0 && g_argv[0]!=0 ){
    for(i=1; g_argv[i]; i++){
      if( dashdash || !ISOPT(g_argv[i]) ) cnt++;
      if( strcmp(g_argv[i],"--")==0 ) dashdash = 1;
    }
  }
  return cnt;
}

char *OptArg(int n)
{
  int i;
  i = argindex(n);
  return i>=0 ? g_argv[i] : 0;
}

void OptErr(int n)
{
  int i;
  i = argindex(n);
  if( i>=0 ) errline(i,0,errstream);
}

void OptPrint(void){
  int i;
  int max, len;
  max = 0;
  for(i=0; op[i].label; i++){
    len = lemonStrlen(op[i].label) + 1;
    switch( op[i].type ){
      case OPT_FLAG:
      case OPT_FFLAG:
        break;
      case OPT_INT:
      case OPT_FINT:
        len += 9;       /* length of "<integer>" */
        break;
      case OPT_DBL:
      case OPT_FDBL:
        len += 6;       /* length of "<real>" */
        break;
      case OPT_STR:
      case OPT_FSTR:
        len += 8;       /* length of "<string>" */
        break;
    }
    if( len>max ) max = len;
  }
  for(i=0; op[i].label; i++){
    switch( op[i].type ){
      case OPT_FLAG:
      case OPT_FFLAG:
        fprintf(errstream,"  -%-*s  %s\n",max,op[i].label,op[i].message);
        break;
      case OPT_INT:
      case OPT_FINT:
        fprintf(errstream,"  -%s<integer>%*s  %s\n",op[i].label,
          (int)(max-lemonStrlen(op[i].label)-9),"",op[i].message);
        break;
      case OPT_DBL:
      case OPT_FDBL:
        fprintf(errstream,"  -%s<real>%*s  %s\n",op[i].label,
          (int)(max-lemonStrlen(op[i].label)-6),"",op[i].message);
        break;
      case OPT_STR:
      case OPT_FSTR:
        fprintf(errstream,"  -%s<string>%*s  %s\n",op[i].label,
          (int)(max-lemonStrlen(op[i].label)-8),"",op[i].message);
        break;
    }
  }
}
/*********************** From the file "parse.c" ****************************/
/*
** Input file parser for the LEMON parser generator.
*/

/* The state of the parser */
enum e_state {
  INITIALIZE,
  WAITING_FOR_DECL_OR_RULE,
  WAITING_FOR_DECL_KEYWORD,
  WAITING_FOR_DECL_ARG,
  WAITING_FOR_PRECEDENCE_SYMBOL,
  WAITING_FOR_ARROW,
  IN_RHS,
  LHS_ALIAS_1,
  LHS_ALIAS_2,
  LHS_ALIAS_3,
  RHS_ALIAS_1,
  RHS_ALIAS_2,
  PRECEDENCE_MARK_1,
  PRECEDENCE_MARK_2,
  RESYNC_AFTER_RULE_ERROR,
  RESYNC_AFTER_DECL_ERROR,
  WAITING_FOR_DESTRUCTOR_SYMBOL,
  WAITING_FOR_DATATYPE_SYMBOL,
  WAITING_FOR_FALLBACK_ID,
  WAITING_FOR_WILDCARD_ID,
  WAITING_FOR_CLASS_ID,
  WAITING_FOR_CLASS_TOKEN,
  WAITING_FOR_TOKEN_NAME,
  WAITING_FOR_MODULE_REQUIRE,
  WAITING_FOR_MODULE_REQUIRE_VERSION,
  WAITING_FOR_MODULE_EXPORT,
  WAITING_FOR_MODULE_IMPORT,
  WAITING_FOR_MODULE_IMPORT_FROM,
  WAITING_FOR_MODULE_IMPORT_END,
  WAITING_FOR_EXPECT_VALUE,
  WAITING_FOR_FIRST_TOKEN_VALUE,
  WAITING_FOR_ERROR_SYNC_TOKEN,
  WAITING_FOR_AST_PREFIX_VALUE,
  WAITING_FOR_AST_NODE_NAME,
  WAITING_FOR_AST_NODE_BODY,
  WAITING_FOR_AST_LIST_NAME,
  WAITING_FOR_AST_LIST_ELEMENT,
  /* v0.4.1: %extends "path.lime" -- expects a quoted string
  ** literal next.  On match, recursively parse the named file
  ** before resuming the current file. */
  WAITING_FOR_EXTENDS_PATH,
  /* v0.4.1: %override_type SYM {Type} -- two-step state machine
  ** identical in shape to %type.  WAITING_FOR_OVERRIDE_TYPE_SYM
  ** matches the symbol name, then transitions to
  ** WAITING_FOR_OVERRIDE_TYPE_BODY which reads the {Type} body
  ** via the standard WAITING_FOR_DECL_ARG mechanism. */
  WAITING_FOR_OVERRIDE_TYPE_SYM
};
struct pstate {
  char *filename;       /* Name of the input file */
  int tokenlineno;      /* Linenumber at which current token starts */
  int errorcnt;         /* Number of errors so far */
  char *tokenstart;     /* Text of current token */
  struct lime *gp;     /* Global state vector */
  enum e_state state;        /* The state of the parser */
  struct symbol *fallback;   /* The fallback token */
  struct symbol *tkclass;    /* Token class symbol */
  struct symbol *lhs;        /* Left-hand side of current rule */
  const char *lhsalias;      /* Alias for the LHS */
  int nrhs;                  /* Number of right-hand side symbols seen */
  struct symbol *rhs[MAXRHS];  /* RHS symbols */
  const char *alias[MAXRHS]; /* Aliases for each RHS symbol (or NULL) */
  struct rule *prevrule;     /* Previous rule parsed */
  const char *declkeyword;   /* Keyword of a declaration */
  char **declargslot;        /* Where the declaration argument should be put */
  int insertLineMacro;       /* Add #line before declaration insert */
  int *decllinenoslot;       /* Where to write declaration line number */
  enum e_assoc declassoc;    /* Assign this association to decl arguments */
  int preccounter;           /* Assign this precedence to decl arguments */
  struct rule *firstrule;    /* Pointer to first rule in the grammar */
  struct rule *lastrule;     /* Pointer to the most recently parsed rule */
  struct rule *alt_group_head; /* First rule in an active `|` alternation
                              ** group, or NULL when not in one.  Set by
                              ** the `|` branch of IN_RHS; cleared when
                              ** the next rule starts (or the group's
                              ** trailing action has been propagated). */
  struct module_dependency *current_dependency; /* Current dependency being parsed */
  /* Lime-Letter-19: heap buffer of inter-directive comments
  ** accumulated by the tokenizer since the previous directive or
  ** rule.  Attached to the next directive/rule's leading-comment
  ** slot when that boundary is recognized.  NULL/0/0 when empty.
  ** Grown via pstate_append_comment(); detached via
  ** pstate_take_pending(). */
  char  *pending_comments;
  size_t pending_comments_len;
  size_t pending_comments_cap;
  /* Lime-Letter-19: comment block taken from pending_comments at
  ** the moment we recognize a `%` directive's start.  Held here
  ** until WAITING_FOR_DECL_KEYWORD assigns it to the matching
  ** lem->X_comment slot via attach_directive_comment().  Freed in
  ** parseonetoken() if a recognized directive has no slot of its
  ** own (e.g. %left / %destructor / %token / %type / %fallback). */
  char *pending_directive_comment;
  /* Lime-Letter-19: comment block taken from pending_comments at
  ** the moment we recognize the LHS non-terminal of a new rule.
  ** Attached to rp->leading_comment by commit_current_rule() on
  ** the first commit of an alternation group; cleared on attach. */
  char *pending_rule_comment;
  /* Lime-Letter-21: the active (open) %token / %type group that
  ** the next same-kind directive will append to.  NULL when no
  ** group is open -- either we haven't seen the kind yet, or the
  ** previous directive was a different kind and closed the run.
  ** A pending leading comment also forces a new group to open
  ** (so the comment is attached as that group's banner).  These
  ** two pointers live on the parser scratch state, not on
  ** struct lime, because the open-group concept only exists
  ** during parsing -- the formatter walks the lem->first_*_group
  ** lists, never these. */
  LimeTokenGroup *current_token_group;
  LimeTypeGroup  *current_type_group;
  /* v0.4.1: %extends bookkeeping.  active_extends is a fixed-size
  ** stack of Strsafe'd filenames currently being recursively
  ** parsed.  extends_depth==0 means we are tokenizing the user-
  ** invoked top-level file; deeper values mean we are inside
  ** parse_lime_file_recursive() called from a `%extends "..."`
  ** directive.  Used both for cycle detection (a path appearing
  ** twice on the stack means a `%extends` cycle) and for diamond-
  ** resolution depth comparisons (see struct rule's
  ** override_depth).  16 levels is plenty -- a real grammar
  ** inheritance chain rarely exceeds 3-4. */
#define LIME_EXTENDS_MAX_DEPTH 16
  const char *active_extends[LIME_EXTENDS_MAX_DEPTH];
  int extends_depth;
  /* v0.4.1: when set, the next rule committed by
  ** commit_current_rule() is treated as a %override target
  ** rather than a plain new rule.  Set by parseonetoken() when
  ** it recognises `%override` (without a trailing `_type`); cleared
  ** unconditionally inside commit_current_rule() so a leftover
  ** flag never leaks into the next rule. */
  int pending_override;
  int pending_override_line;     /* line of `%override` for diagnostics */
  /* v0.4.1: same role for %remove.  The directive is parsed as a
  ** rule SHAPE (LHS + RHS); when the rule's terminating `.` is
  ** hit and pending_remove is set, commit_current_rule() unlinks
  ** the matching rule from the gp list and frees the just-built
  ** struct rule (it was a probe, never destined for the grammar).
  ** %remove takes no action body, so the WAITING_FOR_DECL_OR_RULE
  ** state at `.` is the natural terminator. */
  int pending_remove;
  int pending_remove_line;
};

/*
** Lime-Letter-19: comment-buffer helpers.
**
** The tokenizer (Parse() main loop) calls pstate_append_comment()
** for every contiguous chunk of inter-directive whitespace +
** comment text it encounters.  parseonetoken() calls
** pstate_take_pending() at the moment it recognizes a directive's
** `%` prefix or a rule's LHS non-terminal, transferring ownership
** of the buffer to a transient `pending_directive_comment` /
** `pending_rule_comment` slot.  attach_directive_comment() finally
** moves the comment from that slot into the matching lem->X_comment
** field once the directive's keyword is identified.
**
** Trim policy: the chunk handed to pstate_append_comment() is
** trimmed of leading/trailing ASCII whitespace before append, so
** the formatter's emit (`fprintf(out, "%s\n", comment)`) round-
** trips cleanly without compounding newlines.  Blank lines between
** stacked comments inside a chunk are preserved.
*/
static void pstate_append_comment(struct pstate *psp,
                                  const char *start,
                                  size_t n)
{
  /* Trim ASCII whitespace from both ends of [start, start+n). */
  while( n>0 && (start[0]==' ' || start[0]=='\t' ||
                 start[0]=='\n' || start[0]=='\r') ){
    start++; n--;
  }
  while( n>0 && (start[n-1]==' ' || start[n-1]=='\t' ||
                 start[n-1]=='\n' || start[n-1]=='\r') ){
    n--;
  }
  if( n==0 ) return;

  /* Reserve room for the existing buffer (if any), one separator
  ** newline (if existing), the new chunk, and a NUL terminator. */
  size_t need = psp->pending_comments_len + n + 2;
  if( need > psp->pending_comments_cap ){
    size_t new_cap = psp->pending_comments_cap ? psp->pending_comments_cap*2 : 128;
    while( new_cap < need ) new_cap *= 2;
    char *grown = (char*)lime_realloc(psp->pending_comments, new_cap);
    if( grown==0 ) return;  /* OOM: silently drop the comment */
    psp->pending_comments     = grown;
    psp->pending_comments_cap = new_cap;
  }
  if( psp->pending_comments_len > 0 ){
    psp->pending_comments[psp->pending_comments_len++] = '\n';
  }
  memcpy(psp->pending_comments + psp->pending_comments_len, start, n);
  psp->pending_comments_len += n;
  psp->pending_comments[psp->pending_comments_len] = 0;
}

/*
** Detach pending_comments from psp and return ownership to the
** caller.  psp's buffer is reset to empty (NULL/0/0).  Returns
** NULL when nothing was pending.
*/
static char *pstate_take_pending(struct pstate *psp)
{
  char *out = psp->pending_comments;
  psp->pending_comments     = 0;
  psp->pending_comments_len = 0;
  psp->pending_comments_cap = 0;
  return out;
}

/*
** Attach `psp->pending_directive_comment` to `*slot` and clear the
** pstate slot.  No-op when no comment is pending.  If `*slot` is
** already non-NULL (e.g. the same directive appears twice with a
** comment before each occurrence), the prior comment is freed --
** latest wins.
*/
static void attach_directive_comment(struct pstate *psp, char **slot)
{
  if( psp->pending_directive_comment==0 ) return;
  if( *slot ) lime_free(*slot);
  *slot = psp->pending_directive_comment;
  psp->pending_directive_comment = 0;
}

/*
** Lime-Letter-21: append `sp` to `g->symbols`, growing the array
** geometrically.  Silently drops the symbol on OOM (the symbol is
** still recorded in the global Symbol table, so codegen still
** works; only the formatter's group-emit loses one entry).
*/
static void lime_token_group_append(LimeTokenGroup *g, struct symbol *sp)
{
  if( g==0 ) return;
  if( g->n_symbols >= g->cap_symbols ){
    int new_cap = g->cap_symbols ? g->cap_symbols * 2 : 4;
    struct symbol **grown = (struct symbol **)lime_realloc(
      g->symbols, sizeof(struct symbol *) * (size_t)new_cap);
    if( grown==0 ) return;
    g->symbols = grown;
    g->cap_symbols = new_cap;
  }
  g->symbols[g->n_symbols++] = sp;
}

static void lime_type_group_append(LimeTypeGroup *g, struct symbol *sp)
{
  if( g==0 ) return;
  if( g->n_symbols >= g->cap_symbols ){
    int new_cap = g->cap_symbols ? g->cap_symbols * 2 : 4;
    struct symbol **grown = (struct symbol **)lime_realloc(
      g->symbols, sizeof(struct symbol *) * (size_t)new_cap);
    if( grown==0 ) return;
    g->symbols = grown;
    g->cap_symbols = new_cap;
  }
  g->symbols[g->n_symbols++] = sp;
}

/*
** Lime-Letter-21: open a new %token group, link it onto the
** declaration-order list, and return the new group.  Takes
** ownership of `leading_comment` (heap pointer or NULL) -- on
** OOM the comment is freed to avoid leaking the parser's
** pending_directive_comment slot.
*/
static LimeTokenGroup *lime_new_token_group(struct lime *gp,
                                            char *leading_comment)
{
  LimeTokenGroup *g = (LimeTokenGroup *)lime_calloc(1, sizeof(*g));
  if( g==0 ){
    if( leading_comment ) lime_free(leading_comment);
    return 0;
  }
  g->leading_comment = leading_comment;
  if( gp->last_token_group ){
    gp->last_token_group->next = g;
  }else{
    gp->first_token_group = g;
  }
  gp->last_token_group = g;
  return g;
}

static LimeTypeGroup *lime_new_type_group(struct lime *gp,
                                          char *leading_comment)
{
  LimeTypeGroup *g = (LimeTypeGroup *)lime_calloc(1, sizeof(*g));
  if( g==0 ){
    if( leading_comment ) lime_free(leading_comment);
    return 0;
  }
  g->leading_comment = leading_comment;
  if( gp->last_type_group ){
    gp->last_type_group->next = g;
  }else{
    gp->first_type_group = g;
  }
  gp->last_type_group = g;
  return g;
}

/*
** v0.4.1: helper -- unlink a rule from psp->firstrule/lastrule
** AND from rp->lhs->rule's per-LHS chain.  Used by %remove.  The
** rule is freed by lime_free; rhs / rhsalias share the same
** allocation (calloc trick in commit_current_rule), so a single
** free covers them.  origin/override pointers are Strsafe'd, not
** owned by the rule -- no separate free needed for them.  We do
** NOT decrement psp->gp->nrule because rule indexes are reassigned
** by main() after Parse() returns; leaving a hole in the index
** space here is harmless.
*/
static void unlink_and_free_rule(struct pstate *psp, struct rule *target)
{
  struct rule **pp;
  /* Unlink from the global next-list. */
  if( psp->firstrule == target ){
    psp->firstrule = target->next;
  }else{
    for(pp = &psp->firstrule; *pp && *pp != target; pp = &(*pp)->next){}
    if( *pp ) *pp = target->next;
  }
  if( psp->lastrule == target ){
    /* Walk to find the new tail; rare case (remove last rule). */
    struct rule *r = psp->firstrule;
    psp->lastrule = 0;
    while( r ){ psp->lastrule = r; r = r->next; }
  }
  /* Unlink from the per-LHS chain. */
  if( target->lhs ){
    struct rule **lpp = &target->lhs->rule;
    while( *lpp && *lpp != target ) lpp = &(*lpp)->nextlhs;
    if( *lpp ) *lpp = target->nextlhs;
  }
  /* Free leading_comment if any. */
  if( target->leading_comment ){
    lime_free(target->leading_comment);
  }
  lime_free(target);
}

/*
** Commit the current accumulated rule (psp->lhs, psp->lhsalias,
** psp->rhs[0..nrhs-1], psp->alias[0..nrhs-1]) to the grammar's rule
** list.  Called from the IN_RHS state when a rule-terminator (`.`)
** or an alternation (`|`) is seen.  Leaves psp->prevrule pointing at
** the newly-committed rule (NULL on allocation failure), and does
** not modify psp->state.
**
** v0.4.1: branches on psp->pending_override / psp->pending_remove
** to dispatch to override or remove rather than plain add.
** Diamond detection runs against the existing rule list when
** either flag is set.  See docs/EXTENDS.md for the resolution
** rules (5 distinct cases).
*/
static void commit_current_rule(struct pstate *psp)
{
  struct rule *rp;
  int do_override = psp->pending_override;
  int do_remove   = psp->pending_remove;
  /* Clear flags up front so an early return / error doesn't leak
  ** state into the next rule. */
  psp->pending_override = 0;
  psp->pending_remove = 0;

  rp = (struct rule *)lime_calloc( sizeof(struct rule) +
       sizeof(struct symbol*)*psp->nrhs + sizeof(char*)*psp->nrhs, 1);
  if( rp==0 ){
    ErrorMsg(psp->filename,psp->tokenlineno,
      "Can't allocate enough memory for this rule.");
    psp->errorcnt++;
    psp->prevrule = 0;
    return;
  }
  {
    int i;
    rp->ruleline = psp->tokenlineno;
    rp->rhs = (struct symbol**)&rp[1];
    rp->rhsalias = (const char**)&(rp->rhs[psp->nrhs]);
    for(i=0; i<psp->nrhs; i++){
      rp->rhs[i] = psp->rhs[i];
      rp->rhsalias[i] = psp->alias[i];
      if( rp->rhsalias[i]!=0 ){ rp->rhs[i]->bContent = 1; }
    }
    rp->lhs = psp->lhs;
    rp->lhsalias = psp->lhsalias;
    rp->nrhs = psp->nrhs;
    rp->code = 0;
    rp->noCode = 1;
    rp->precsym = 0;
    /* v0.4.1: provenance for diamond resolution + override matching. */
    rp->origin_file = psp->filename ? Strsafe(psp->filename) : 0;
    rp->origin_line = psp->tokenlineno;
    rp->override_depth = INT_MAX;
    rp->is_overridden = 0;
    rp->conflict_pending = 0;
    rp->override_file = 0;
    rp->conflict_file = 0;
    rp->conflict_line = 0;
  }

  /* v0.4.1: dispatch on override / remove flags before anything
  ** that mutates the gp list.  Both branches consume `rp` -- it
  ** was built only as a probe with the right identity. */
  if( do_remove ){
    struct rule *target = find_rule_by_identity(psp, rp);
    if( target==0 ){
#ifdef LIME_STRICT
      ErrorMsg(psp->filename, psp->pending_remove_line,
        "%%remove: no rule of matching identity (LHS=%s, %d RHS) "
        "to remove",
        rp->lhs ? rp->lhs->name : "?", rp->nrhs);
      psp->errorcnt++;
#else
      fprintf(stderr,
        "%s:%d: warning: %%remove targets non-existent rule "
        "(LHS=%s, %d RHS); ignoring\n",
        psp->filename, psp->pending_remove_line,
        rp->lhs ? rp->lhs->name : "?", rp->nrhs);
#endif
      lime_free(rp);
      psp->prevrule = 0;
      return;
    }
    /* Diamond rule 8: %override on one path + %remove on another
    ** at the same depth = conflict.  We err on the side of
    ** removing (the user's most recent stated intent), but mark
    ** the rule for end-of-Parse() conflict reporting if the
    ** existing rule was overridden by a sibling.  Since the rule
    ** is going away, we can't usefully mark it; emit the error
    ** synchronously. */
    if( target->is_overridden
        && target->override_depth >= psp->extends_depth
        && target->override_file != 0
        && target->override_file != Strsafe(psp->filename) ){
      ErrorMsg(psp->filename, psp->pending_remove_line,
        "%%remove conflicts with %%override of '%s' from '%s' "
        "on a sibling %%extends path; add a %%override (or %%remove) "
        "in the derived file to disambiguate",
        target->lhs ? target->lhs->name : "?",
        target->override_file);
      psp->errorcnt++;
    }
    unlink_and_free_rule(psp, target);
    lime_free(rp);
    psp->prevrule = 0;
    return;
  }

  if( do_override ){
    struct rule *target = find_rule_by_identity(psp, rp);
    if( target==0 ){
      ErrorMsg(psp->filename, psp->pending_override_line,
        "%%override: no rule of matching identity (LHS=%s, %d RHS) "
        "to override -- the directive replaces a rule inherited "
        "via %%extends; check the LHS and RHS sequence match",
        rp->lhs ? rp->lhs->name : "?", rp->nrhs);
      psp->errorcnt++;
      lime_free(rp);
      psp->prevrule = 0;
      return;
    }
    /* Diamond resolution.  See docs/EXTENDS.md "Override-vs-override". */
    int new_depth = psp->extends_depth;
    const char *new_file = rp->origin_file;
    if( target->is_overridden ){
      if( new_depth < target->override_depth ){
        /* Shallower override wins (Rule 6). */
        target->conflict_pending = 0;
        target->conflict_file = 0;
      }else if( new_depth > target->override_depth ){
        /* More-derived override already on the rule; skip. */
        lime_free(rp);
        psp->prevrule = target;
        return;
      }else{
        /* Same depth. */
        if( target->override_file == new_file ){
          /* Same file overriding twice: last-wins. */
        }else{
          /* Different file at same depth = diamond conflict
          ** (Rule 4).  Mark for end-of-Parse() error, but
          ** still apply the new body so downstream codegen
          ** has a deterministic rule.  The conflict_file slot
          ** records the LOSING side so the diagnostic can
          ** name both. */
          target->conflict_pending = 1;
          target->conflict_file = target->override_file;
          target->conflict_line = target->ruleline;
        }
      }
    }
    /* Apply the override: replace body, aliases, precsym; preserve
    ** rule index and per-LHS chain links so reduce-action numbering
    ** stays stable. */
    {
      int i;
      target->lhsalias = rp->lhsalias;
      for(i=0; i<target->nrhs && i<rp->nrhs; i++){
        target->rhsalias[i] = rp->rhsalias[i];
      }
      target->ruleline = rp->ruleline;
      target->origin_file = rp->origin_file;
      target->origin_line = rp->origin_line;
      target->is_overridden = 1;
      target->override_depth = new_depth;
      target->override_file = new_file;
      /* Clear the prior code/precsym so the rule's trailing
      ** action+precedence parses fill them on `target` rather
      ** than `rp` (which we're about to throw away). */
      target->code = 0;
      target->codePrefix = 0;
      target->codeSuffix = 0;
      target->noCode = 1;
      target->precsym = 0;
      target->line = 0;
    }
    lime_free(rp);
    /* Post-override the trailing `{ action }` and `[PRECSYM]`
    ** parsers in WAITING_FOR_DECL_OR_RULE attach to psp->prevrule.
    ** Point that at the in-place-replaced target so the body
    ** lands in the right slot. */
    psp->prevrule = target;
    return;
  }

  /* Plain rule add: diamond-deduplication.  If a rule with the
  ** same identity already exists, we silently skip the new copy.
  ** Two channels feed this case:
  **   1. The same base file is loaded twice via different
  **      %extends paths (the diamond pattern); the rule is the
  **      same on both arms, so dropping the second copy is
  **      lossless.
  **   2. A previously-overridden rule has been re-encountered as
  **      a plain re-add from a sibling base file that doesn't
  **      know about the override; the override stands per Rule 5. */
  {
    struct rule *existing = find_rule_by_identity(psp, rp);
    if( existing != 0 ){
      lime_free(rp);
      /* Point prevrule at the existing rule so that any trailing
      ** action attached to this duplicate writes onto the
      ** existing.  This matters when a base file is parsed
      ** through a diamond: the second time around we want the
      ** action body (if any) to land on the SAME rule object.
      ** If the existing rule already has a code body, the
      ** "is not the first to follow the previous rule" guard
      ** in WAITING_FOR_DECL_OR_RULE will reject the second
      ** action -- which is what we want for diamonds where
      ** both paths see the same body verbatim (Strsafe-interned
      ** string equality means subsequent attaches to the same
      ** rp->code pointer are no-ops, so the guard fires only on
      ** truly differing bodies, but parseonetoken doesn't know
      ** that, so it still errors -- acceptable trade-off). */
      psp->prevrule = existing;
      return;
    }
  }

  /* Lime-Letter-19 leading-comment attach happens here.  See the
  ** comment in the previous block for ownership semantics. */
  rp->leading_comment = psp->pending_rule_comment;
  psp->pending_rule_comment = 0;
  rp->index = psp->gp->nrule++;
  rp->nextlhs = rp->lhs->rule;
  rp->lhs->rule = rp;
  rp->next = 0;
  if( psp->firstrule==0 ){
    psp->firstrule = psp->lastrule = rp;
  }else{
    psp->lastrule->next = rp;
    psp->lastrule = rp;
  }
  psp->prevrule = rp;
}

/*
** Propagate attributes that attach to the last rule of a `|`
** alternation group (trailing action code, precedence mark,
** NEVER-REDUCE flag) across every alternative in the group.
**
** psp->alt_group_head is the first rule in the group; psp->prevrule
** is the last.  For each rule in the half-open range
** [alt_group_head, prevrule), copy code / line / noCode / precsym /
** neverReduce from prevrule.  After propagation the group is
** considered consumed and alt_group_head is cleared, so subsequent
** attaches (if any somehow arrive) do not re-propagate.
**
** Safe to call unconditionally; becomes a no-op when alt_group_head
** is NULL or equal to prevrule (single-alternative case).
*/
static void propagate_alt_group_attach(struct pstate *psp)
{
  struct rule *head = psp->alt_group_head;
  struct rule *last = psp->prevrule;
  struct rule *rp;
  if( head==0 || last==0 || head==last ) {
    psp->alt_group_head = 0;
    return;
  }
  for(rp=head; rp!=last && rp!=0; rp=rp->next){
    if( rp->code==0 ){
      rp->code      = last->code;
      rp->line      = last->line;
      rp->noCode    = last->noCode;
    }
    if( rp->precsym==0 ){
      rp->precsym   = last->precsym;
    }
    if( !rp->neverReduce ){
      rp->neverReduce = last->neverReduce;
    }
  }
  psp->alt_group_head = 0;
}

/* Parse a single token */
static void parseonetoken(struct pstate *psp)
{
  const char *x;
  x = Strsafe(psp->tokenstart);     /* Save the token permanently */
#if 0
  printf("%s:%d: Token=[%s] state=%d\n",psp->filename,psp->tokenlineno,
    x,psp->state);
#endif
  /* Lime-Letter-19: comments captured between the previous token and
  ** this one are only meaningful at a directive/rule boundary.  When
  ** we're in the middle of parsing an argument list, a precedence
  ** symbol list, or a rule's RHS, drop the buffer -- there's no slot
  ** to attach it to.  The boundary cases (WAITING_FOR_DECL_OR_RULE
  ** and INITIALIZE) handle the buffer themselves below. */
  if( psp->state != WAITING_FOR_DECL_OR_RULE
   && psp->state != INITIALIZE ){
    char *drop = pstate_take_pending(psp);
    if( drop ) lime_free(drop);
  }
  switch( psp->state ){
    case INITIALIZE:
      psp->prevrule = 0;
      psp->preccounter = 0;
      psp->firstrule = psp->lastrule = 0;
      psp->alt_group_head = 0;
      psp->gp->nrule = 0;
      /* fall through */
    case WAITING_FOR_DECL_OR_RULE:
      if( x[0]=='%' ){
        /* Leaving the post-rule window without a trailing action or
        ** precedence mark: the alternation group (if any) ends here
        ** with no attributes to propagate. */
        psp->alt_group_head = 0;
        /* Lime-Letter-19: take any pending comments as the leading
        ** comment for the directive whose keyword arrives next.
        ** Free any previously-held but unattached comment first --
        ** that means the prior directive had no _comment slot of
        ** its own (e.g. %left, %destructor) and the comment is
        ** silently dropped.  attach_directive_comment() in
        ** WAITING_FOR_DECL_KEYWORD will clear pending_directive_
        ** comment on a successful attach. */
        if( psp->pending_directive_comment ){
          lime_free(psp->pending_directive_comment);
          psp->pending_directive_comment = 0;
        }
        psp->pending_directive_comment = pstate_take_pending(psp);
        psp->state = WAITING_FOR_DECL_KEYWORD;
      }else if( ISLOWER(x[0]) ){
        /* New rule starts: same reasoning -- close out any dangling
        ** alternation group. */
        psp->alt_group_head = 0;
        /* Lime-Letter-19: take any pending comments as the leading
        ** comment for this rule.  commit_current_rule() will move
        ** them onto rp->leading_comment when the rule completes. */
        if( psp->pending_rule_comment ){
          lime_free(psp->pending_rule_comment);
          psp->pending_rule_comment = 0;
        }
        psp->pending_rule_comment = pstate_take_pending(psp);
        psp->lhs = Symbol_new(x);
        psp->nrhs = 0;
        psp->lhsalias = 0;
        psp->state = WAITING_FOR_ARROW;
      }else if( x[0]=='{' ){
        /* Attaching an action body to the prior rule.  Comments
        ** between the rule's `.` and the `{` have no good home
        ** (they belong to neither rule); discard. */
        {
          char *drop = pstate_take_pending(psp);
          if( drop ) lime_free(drop);
        }
        if( psp->prevrule==0 ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "There is no prior rule upon which to attach the code "
            "fragment which begins on this line.");
          psp->errorcnt++;
        }else if( psp->prevrule->code!=0 ){
          if( psp->extends_depth > 0 ){
            /* v0.4.1: the previous "rule" was actually deduped
            ** against an existing rule from a prior %extends arm
            ** (or this is the same base file pulled in twice via
            ** a diamond).  The existing rule already carries a
            ** code body; silently swallow the redundant one.
            ** A genuine conflict would be flagged at end-of-Parse()
            ** by the diamond conflict sweep, not here. */
          }else{
            ErrorMsg(psp->filename,psp->tokenlineno,
              "Code fragment beginning on this line is not the first "
              "to follow the previous rule.");
            psp->errorcnt++;
          }
        }else if( strcmp(x, "{NEVER-REDUCE")==0 ){
          psp->prevrule->neverReduce = 1;
          propagate_alt_group_attach(psp);
        }else{
          psp->prevrule->line = psp->tokenlineno;
          psp->prevrule->code = &x[1];
          psp->prevrule->noCode = 0;
          propagate_alt_group_attach(psp);
        }
      }else if( x[0]=='[' ){
        /* Same reasoning as `{`: discard any pending comments. */
        {
          char *drop = pstate_take_pending(psp);
          if( drop ) lime_free(drop);
        }
        psp->state = PRECEDENCE_MARK_1;
      }else{
        /* Error path: discard pending comments to avoid carrying them
        ** into a later boundary that has no relation to them. */
        {
          char *drop = pstate_take_pending(psp);
          if( drop ) lime_free(drop);
        }
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Token \"%s\" should be either \"%%\" or a nonterminal name.",
          x);
        psp->errorcnt++;
      }
      break;
    case PRECEDENCE_MARK_1:
      if( !ISUPPER(x[0]) ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "The precedence symbol must be a terminal.");
        psp->errorcnt++;
      }else if( psp->prevrule==0 ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "There is no prior rule to assign precedence \"[%s]\".",x);
        psp->errorcnt++;
      }else if( psp->prevrule->precsym!=0 ){
        if( psp->extends_depth > 0 ){
          /* v0.4.1: diamond reload -- existing rule already has
          ** a precedence mark from the first arm.  Silently
          ** swallow the redundant one. */
        }else{
          ErrorMsg(psp->filename,psp->tokenlineno,
            "Precedence mark on this line is not the first "
            "to follow the previous rule.");
          psp->errorcnt++;
        }
      }else{
        psp->prevrule->precsym = Symbol_new(x);
      }
      psp->state = PRECEDENCE_MARK_2;
      break;
    case PRECEDENCE_MARK_2:
      if( x[0]!=']' ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing \"]\" on precedence mark.");
        psp->errorcnt++;
      }
      /* Precedence mark (like a trailing action) attaches to the
      ** last-committed rule; propagate to every alternative in the
      ** group if we were in one. */
      propagate_alt_group_attach(psp);
      psp->state = WAITING_FOR_DECL_OR_RULE;
      break;
    case WAITING_FOR_ARROW:
      if( x[0]==':' && x[1]==':' && x[2]=='=' ){
        psp->state = IN_RHS;
      }else if( x[0]=='(' ){
        psp->state = LHS_ALIAS_1;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Expected to see a \":\" following the LHS symbol \"%s\".",
          psp->lhs->name);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case LHS_ALIAS_1:
      if( ISALPHA(x[0]) ){
        psp->lhsalias = x;
        psp->state = LHS_ALIAS_2;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "\"%s\" is not a valid alias for the LHS \"%s\"\n",
          x,psp->lhs->name);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case LHS_ALIAS_2:
      if( x[0]==')' ){
        psp->state = LHS_ALIAS_3;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing \")\" following LHS alias name \"%s\".",psp->lhsalias);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case LHS_ALIAS_3:
      if( x[0]==':' && x[1]==':' && x[2]=='=' ){
        psp->state = IN_RHS;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing \"->\" following: \"%s(%s)\".",
           psp->lhs->name,psp->lhsalias);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case IN_RHS:
      if( x[0]=='.' ){
        commit_current_rule(psp);
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( x[0]=='|' && x[1]==0 ){
        /* Bison-compat rule alternation: `lhs ::= A B | C D | E.` is
        ** sugar for three separate `lhs ::= ...` rules that share an
        ** LHS (and, if present, a single trailing action block).
        ** Commit what we have, then reset the RHS accumulator and
        ** stay in IN_RHS with the same LHS and LHS alias so the next
        ** alternative starts parsing immediately.  An empty RHS
        ** between `::=` and `|` (epsilon alternative) is allowed.
        **
        ** Lime actions go *after* the rule's terminating `.`, not
        ** inline per alternative.  We remember the first rule of the
        ** alternation group here; when the trailing `{ ... }` block
        ** arrives it is duplicated across every rule in the group so
        ** all alternatives run the same action when they reduce.
        ** (See the `{` branch of WAITING_FOR_DECL_OR_RULE.)
        ** If you need *different* actions per alternative, expand to
        ** separate `lhs ::= ...` rules by hand. */
        commit_current_rule(psp);
        if( psp->alt_group_head==0 && psp->prevrule!=0 ){
          psp->alt_group_head = psp->prevrule;
        }
        psp->nrhs = 0;
        /* psp->state stays IN_RHS; psp->lhs and psp->lhsalias stay. */
      }else if( ISALPHA(x[0]) ){
        if( psp->nrhs>=MAXRHS ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "Too many symbols on RHS of rule beginning at \"%s\".",
            x);
          psp->errorcnt++;
          psp->state = RESYNC_AFTER_RULE_ERROR;
        }else{
          psp->rhs[psp->nrhs] = Symbol_new(x);
          psp->alias[psp->nrhs] = 0;
          psp->nrhs++;
        }
      }else if( (x[0]=='|' || x[0]=='/') && psp->nrhs>0 && ISUPPER(x[1]) ){
        struct symbol *msp = psp->rhs[psp->nrhs-1];
        if( msp->type!=MULTITERMINAL ){
          struct symbol *origsp = msp;
          msp = (struct symbol *) lime_calloc(1,sizeof(*msp));
          memset(msp, 0, sizeof(*msp));
          msp->type = MULTITERMINAL;
          msp->nsubsym = 1;
          msp->subsym = (struct symbol**)lime_calloc(1,sizeof(struct symbol*));
          msp->subsym[0] = origsp;
          msp->name = origsp->name;
          psp->rhs[psp->nrhs-1] = msp;
        }
        msp->nsubsym++;
        msp->subsym = (struct symbol **) lime_realloc(msp->subsym,
          sizeof(struct symbol*)*msp->nsubsym);
        msp->subsym[msp->nsubsym-1] = Symbol_new(&x[1]);
        if( ISLOWER(x[1]) || ISLOWER(msp->subsym[0]->name[0]) ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "Cannot form a compound containing a non-terminal");
          psp->errorcnt++;
        }
      }else if( x[0]=='(' && psp->nrhs>0 ){
        psp->state = RHS_ALIAS_1;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Illegal character on RHS of rule: \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case RHS_ALIAS_1:
      if( ISALPHA(x[0]) ){
        psp->alias[psp->nrhs-1] = x;
        psp->state = RHS_ALIAS_2;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "\"%s\" is not a valid alias for the RHS symbol \"%s\"\n",
          x,psp->rhs[psp->nrhs-1]->name);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case RHS_ALIAS_2:
      if( x[0]==')' ){
        psp->state = IN_RHS;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing \")\" following LHS alias name \"%s\".",psp->lhsalias);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_RULE_ERROR;
      }
      break;
    case WAITING_FOR_DECL_KEYWORD:
      if( ISALPHA(x[0]) ){
        /* Lime-Letter-21: a non-%token / non-%type directive ends
        ** the current per-kind group run.  Save the open groups
        ** locally and clear before the strcmp ladder so the
        ** default behaviour for any directive is "close the runs";
        ** the %token / %type branches restore their saved pointer
        ** when they decide to append rather than open a new group. */
        LimeTokenGroup *saved_token_group = psp->current_token_group;
        LimeTypeGroup  *saved_type_group  = psp->current_type_group;
        psp->current_token_group = 0;
        psp->current_type_group  = 0;
        psp->declkeyword = x;
        psp->declargslot = 0;
        psp->decllinenoslot = 0;
        psp->insertLineMacro = 1;
        psp->state = WAITING_FOR_DECL_ARG;
        if( strcmp(x,"name")==0 ){
          psp->declargslot = &(psp->gp->name);
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->name_comment);
        }else if( strcmp(x,"name_prefix")==0 ){
          /* Bison-compat alias for %name.  Bison spells this %name-prefix
          ** with a dash, which Lime's directive tokenizer cannot accept;
          ** a bison->lime converter translates the dash form to this. */
          psp->declargslot = &(psp->gp->name);
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->name_comment);
        }else if( strcmp(x,"include")==0 ){
          psp->declargslot = &(psp->gp->include);
          attach_directive_comment(psp, &psp->gp->include_comment);
        }else if( strcmp(x,"code")==0 ){
          psp->declargslot = &(psp->gp->extracode);
        }else if( strcmp(x,"token_destructor")==0 ){
          psp->declargslot = &psp->gp->tokendest;
          attach_directive_comment(psp, &psp->gp->tokendest_comment);
        }else if( strcmp(x,"default_destructor")==0 ){
          psp->declargslot = &psp->gp->vardest;
          attach_directive_comment(psp, &psp->gp->vardest_comment);
        }else if( strcmp(x,"token_prefix")==0 ){
          psp->declargslot = &psp->gp->tokenprefix;
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->tokenprefix_comment);
        }else if( strcmp(x,"symbol_prefix")==0 ){
          /* Berkeley-DB-style namespace prefix for the internal
          ** YY_* macros and yy* types/functions Lime emits.  The
          ** generator emits a header block at the top of the .c
          ** file that #define-aliases each internal name to
          ** <prefix><name>, so the preprocessor renames every use
          ** without us having to edit limpar.c.  Any non-empty
          ** string is accepted; conventionally it ends in '_'.
          ** See ReportTable() for the emitted block. */
          psp->declargslot = &psp->gp->symbolprefix;
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->symbolprefix_comment);
        }else if( strcmp(x,"syntax_error")==0 ){
          psp->declargslot = &(psp->gp->error);
          attach_directive_comment(psp, &psp->gp->error_comment);
        }else if( strcmp(x,"parse_accept")==0 ){
          psp->declargslot = &(psp->gp->accept);
          attach_directive_comment(psp, &psp->gp->accept_comment);
        }else if( strcmp(x,"parse_failure")==0 ){
          psp->declargslot = &(psp->gp->failure);
          attach_directive_comment(psp, &psp->gp->failure_comment);
        }else if( strcmp(x,"stack_overflow")==0 ){
          psp->declargslot = &(psp->gp->overflow);
          attach_directive_comment(psp, &psp->gp->overflow_comment);
        }else if( strcmp(x,"extra_argument")==0 ){
          psp->declargslot = &(psp->gp->arg);
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->arg_comment);
        }else if( strcmp(x,"extra_context")==0 ){
          psp->declargslot = &(psp->gp->ctx);
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->ctx_comment);
        }else if( strcmp(x,"token_type")==0 ){
          psp->declargslot = &(psp->gp->tokentype);
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->tokentype_comment);
        }else if( strcmp(x,"location_type")==0 ){
          /* Override of the default LimeLocation YYLOCATIONTYPE.
          ** Reuses the same brace-content state as %token_type:
          ** %location_type {int} or %location_type {struct YYLTYPE}. */
          psp->declargslot = &(psp->gp->location_type);
          psp->insertLineMacro = 0;
        }else if( strcmp(x,"default_type")==0 ){
          psp->declargslot = &(psp->gp->vartype);
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->vartype_comment);
        }else if( strcmp(x,"stack_size_limit")==0 ){
          psp->declargslot = &(psp->gp->stackSizeLimit);
          psp->insertLineMacro = 0;
        }else if( strcmp(x,"realloc")==0 ){
          psp->declargslot = &(psp->gp->reallocFunc);
          psp->insertLineMacro = 0;
        }else if( strcmp(x,"free")==0 ){
          psp->declargslot = &(psp->gp->freeFunc);
          psp->insertLineMacro = 0;
        }else if( strcmp(x,"stack_size")==0 ){
          psp->declargslot = &(psp->gp->stacksize);
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->stacksize_comment);
        }else if( strcmp(x,"start_symbol")==0 ){
          psp->declargslot = &(psp->gp->start);
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->start_comment);
        }else if( strcmp(x,"start")==0 ){
          /* Bison-compat alias for %start_symbol. */
          psp->declargslot = &(psp->gp->start);
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->start_comment);
        }else if( strcmp(x,"left")==0 ){
          psp->preccounter++;
          psp->declassoc = LEFT;
          psp->state = WAITING_FOR_PRECEDENCE_SYMBOL;
        }else if( strcmp(x,"right")==0 ){
          psp->preccounter++;
          psp->declassoc = RIGHT;
          psp->state = WAITING_FOR_PRECEDENCE_SYMBOL;
        }else if( strcmp(x,"nonassoc")==0 ){
          psp->preccounter++;
          psp->declassoc = NONE;
          psp->state = WAITING_FOR_PRECEDENCE_SYMBOL;
        }else if( strcmp(x,"destructor")==0 ){
          psp->state = WAITING_FOR_DESTRUCTOR_SYMBOL;
        }else if( strcmp(x,"type")==0 ){
          /* Lime-Letter-21: open or extend the current %type group.
          ** A pending leading comment forces a new group; otherwise
          ** we append to the saved one.  Either way, %token's
          ** group run is closed (cleared above by the saved-and-
          ** clear at the top of WAITING_FOR_DECL_KEYWORD). */
          {
            char *lc = psp->pending_directive_comment;
            psp->pending_directive_comment = 0;
            if( lc!=0 || saved_type_group==0 ){
              psp->current_type_group =
                lime_new_type_group(psp->gp, lc);
            }else{
              psp->current_type_group = saved_type_group;
              /* lc is 0 here -- nothing to free. */
            }
          }
          psp->state = WAITING_FOR_DATATYPE_SYMBOL;
        }else if( strcmp(x,"fallback")==0 ){
          psp->fallback = 0;
          psp->state = WAITING_FOR_FALLBACK_ID;
        }else if( strcmp(x,"token")==0 ){
          /* Lime-Letter-21: open or extend the current %token
          ** group.  Same logic as %type above. */
          {
            char *lc = psp->pending_directive_comment;
            psp->pending_directive_comment = 0;
            if( lc!=0 || saved_token_group==0 ){
              psp->current_token_group =
                lime_new_token_group(psp->gp, lc);
            }else{
              psp->current_token_group = saved_token_group;
            }
          }
          psp->state = WAITING_FOR_TOKEN_NAME;
        }else if( strcmp(x,"wildcard")==0 ){
          psp->state = WAITING_FOR_WILDCARD_ID;
        }else if( strcmp(x,"token_class")==0 ){
          psp->state = WAITING_FOR_CLASS_ID;
        }else if( strcmp(x,"module_name")==0 ){
          psp->declargslot = &(psp->gp->module_name);
          psp->insertLineMacro = 0;
          attach_directive_comment(psp, &psp->gp->module_comment);
        }else if( strcmp(x,"module_version")==0 ){
          psp->declargslot = &(psp->gp->module_version);
          psp->insertLineMacro = 0;
        }else if( strcmp(x,"module_description")==0 ){
          psp->declargslot = &(psp->gp->module_description);
          psp->insertLineMacro = 0;
        }else if( strcmp(x,"require")==0 ){
          psp->state = WAITING_FOR_MODULE_REQUIRE;
        }else if( strcmp(x,"export")==0 ){
          psp->state = WAITING_FOR_MODULE_EXPORT;
        }else if( strcmp(x,"import")==0 ){
          psp->state = WAITING_FOR_MODULE_IMPORT;
        }else if( strcmp(x,"expect")==0 ){
          psp->state = WAITING_FOR_EXPECT_VALUE;
          attach_directive_comment(psp, &psp->gp->nexpect_comment);
        }else if( strcmp(x,"first_token")==0 ){
          psp->state = WAITING_FOR_FIRST_TOKEN_VALUE;
        }else if( strcmp(x,"locations")==0 ){
          psp->gp->has_locations = 1;
          psp->state = WAITING_FOR_DECL_OR_RULE;
        }else if( strcmp(x,"error_sync")==0 ){
          psp->state = WAITING_FOR_ERROR_SYNC_TOKEN;
        }else if( strcmp(x,"ast_prefix")==0 ){
          psp->state = WAITING_FOR_AST_PREFIX_VALUE;
        }else if( strcmp(x,"ast_node")==0 ){
          psp->state = WAITING_FOR_AST_NODE_NAME;
        }else if( strcmp(x,"ast_list")==0 ){
          psp->state = WAITING_FOR_AST_LIST_NAME;
        }else if( strcmp(x,"ast_auto")==0 ){
          psp->gp->ast_auto = 1;
          psp->state = WAITING_FOR_DECL_OR_RULE;
        }else if( strcmp(x,"extends")==0 ){
          /* v0.4.1: %extends "path" -- recursively load the named
          ** Lime grammar file before continuing the current one.
          ** The directive may appear anywhere a normal directive
          ** does; conventionally it sits at the top of the
          ** derived file.  See docs/EXTENDS.md. */
          psp->state = WAITING_FOR_EXTENDS_PATH;
        }else if( strcmp(x,"override")==0 ){
          /* v0.4.1: %override -- the next rule REPLACES a rule of
          ** matching identity (LHS + RHS sequence) inherited from a
          ** %extends'd file.  Errors at commit time if no match
          ** found.  Diamond resolution: a shallower-depth override
          ** wins; same-depth overrides from different files are a
          ** conflict (collected and errored at end of Parse()). */
          if( psp->pending_override ){
            ErrorMsg(psp->filename,psp->tokenlineno,
              "%%override repeated without an intervening rule");
            psp->errorcnt++;
          }
          psp->pending_override = 1;
          psp->pending_override_line = psp->tokenlineno;
          /* No state change -- the next token is expected to be the
          ** LHS of a normal-looking rule, which will be picked up
          ** by WAITING_FOR_DECL_OR_RULE. */
          psp->state = WAITING_FOR_DECL_OR_RULE;
          /* Drop any pending leading comment; it would otherwise
          ** be attached to the override-target rule, but the
          ** target rule's leading_comment slot is already owned
          ** by the base file's declaration of it. */
          if( psp->pending_directive_comment ){
            lime_free(psp->pending_directive_comment);
            psp->pending_directive_comment = 0;
          }
        }else if( strcmp(x,"override_type")==0 ){
          /* v0.4.1: %override_type SYM {Type}.  Mirrors %type's
          ** state machine but without the "already defined"
          ** guard -- widening an existing type IS the point.
          ** Lime warns at codegen time when a widening actually
          ** happens (the user is asserting ABI compatibility). */
          psp->state = WAITING_FOR_OVERRIDE_TYPE_SYM;
          if( psp->pending_directive_comment ){
            lime_free(psp->pending_directive_comment);
            psp->pending_directive_comment = 0;
          }
        }else if( strcmp(x,"remove")==0 ){
          /* v0.4.1: %remove rule-shape.  -- delete a rule of
          ** matching identity from the merged grammar.  No action
          ** body; the directive ends at the rule's terminating
          ** `.`.  Soft-fails (warn) when the target rule does
          ** not exist, unless LIME_STRICT is defined (see
          ** meson.build), in which case the same condition is
          ** a hard error.  Diamond conflict (override on one path,
          ** remove on another at the same depth) is errored at
          ** end-of-Parse(). */
          if( psp->pending_remove ){
            ErrorMsg(psp->filename,psp->tokenlineno,
              "%%remove repeated without an intervening rule");
            psp->errorcnt++;
          }
          psp->pending_remove = 1;
          psp->pending_remove_line = psp->tokenlineno;
          psp->state = WAITING_FOR_DECL_OR_RULE;
          if( psp->pending_directive_comment ){
            lime_free(psp->pending_directive_comment);
            psp->pending_directive_comment = 0;
          }
        }else{
          ErrorMsg(psp->filename,psp->tokenlineno,
            "Unknown declaration keyword: \"%%%s\".",x);
          psp->errorcnt++;
          psp->state = RESYNC_AFTER_DECL_ERROR;
        }
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Illegal declaration keyword: \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_DESTRUCTOR_SYMBOL:
      if( !ISALPHA(x[0]) ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Symbol name missing after %%destructor keyword");
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }else{
        struct symbol *sp = Symbol_new(x);
        psp->declargslot = &sp->destructor;
        psp->decllinenoslot = &sp->destLineno;
        psp->insertLineMacro = 1;
        psp->state = WAITING_FOR_DECL_ARG;
      }
      break;
    case WAITING_FOR_DATATYPE_SYMBOL:
      if( !ISALPHA(x[0]) ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Symbol name missing after %%type keyword");
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }else{
        struct symbol *sp = Symbol_find(x);
        if((sp) && (sp->datatype) && psp->extends_depth==0){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "Symbol %%type \"%s\" already defined", x);
          psp->errorcnt++;
          psp->state = RESYNC_AFTER_DECL_ERROR;
        }else if((sp) && (sp->datatype)){
          /* v0.4.1: diamond %extends reload -- the same base file
          ** has been pulled in via two paths and is re-declaring a
          ** %type we already have.  Silently consume the {body}
          ** without overwriting the existing string by routing
          ** WAITING_FOR_DECL_ARG to a static throwaway slot.  This
          ** is correct for IDENTICAL re-declarations (the diamond
          ** convergence case); a CONFLICTING re-declaration would
          ** silently take the first-loaded version, which is the
          ** same first-wins policy applied to plain rules. */
          static char *type_discard_slot = 0;
          if( type_discard_slot ){
            lime_free(type_discard_slot);
            type_discard_slot = 0;
          }
          psp->declargslot = &type_discard_slot;
          psp->insertLineMacro = 0;
          psp->state = WAITING_FOR_DECL_ARG;
        }else{
          if (!sp){
            sp = Symbol_new(x);
          }
          psp->declargslot = &sp->datatype;
          psp->insertLineMacro = 0;
          psp->state = WAITING_FOR_DECL_ARG;
          /* Lime-Letter-21: record this symbol in the active %type
          ** group (opened by WAITING_FOR_DECL_KEYWORD when it saw
          ** "type").  NULL group means OOM upstream -- silently
          ** drop, codegen still works. */
          lime_type_group_append(psp->current_type_group, sp);
        }
      }
      break;
    case WAITING_FOR_PRECEDENCE_SYMBOL:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( ISUPPER(x[0]) ){
        struct symbol *sp;
        sp = Symbol_new(x);
        if( sp->prec>=0 ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "Symbol \"%s\" has already be given a precedence.",x);
          psp->errorcnt++;
        }else{
          sp->prec = psp->preccounter;
          sp->assoc = psp->declassoc;
        }
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Can't assign a precedence to \"%s\".",x);
        psp->errorcnt++;
      }
      break;
    case WAITING_FOR_DECL_ARG:
      if( x[0]=='{' || x[0]=='\"' || ISALNUM(x[0]) ){
        const char *zOld, *zNew;
        char *zBuf, *z;
        int nOld, n, nLine = 0, nNew, nBack;
        int addLineMacro;
        char zLine[50];
        zNew = x;
        if( zNew[0]=='"' || zNew[0]=='{' ) zNew++;
        nNew = lemonStrlen(zNew);
        if( *psp->declargslot ){
          zOld = *psp->declargslot;
        }else{
          zOld = "";
        }
        nOld = lemonStrlen(zOld);
        n = nOld + nNew + 20;
        addLineMacro = !psp->gp->nolinenosflag
                       && psp->insertLineMacro
                       && psp->tokenlineno>1
                       && (psp->decllinenoslot==0 || psp->decllinenoslot[0]!=0);
        if( addLineMacro ){
          for(z=psp->filename, nBack=0; *z; z++){
            if( *z=='\\' ) nBack++;
          }
          lemon_sprintf(zLine, "#line %d ", psp->tokenlineno);
          nLine = lemonStrlen(zLine);
          n += nLine + lemonStrlen(psp->filename) + nBack;
        }
        *psp->declargslot = (char *) lime_realloc(*psp->declargslot, n);
        zBuf = *psp->declargslot + nOld;
        if( addLineMacro ){
          if( nOld && zBuf[-1]!='\n' ){
            *(zBuf++) = '\n';
          }
          memcpy(zBuf, zLine, nLine);
          zBuf += nLine;
          *(zBuf++) = '"';
          for(z=psp->filename; *z; z++){
            if( *z=='\\' ){
              *(zBuf++) = '\\';
            }
            *(zBuf++) = *z;
          }
          *(zBuf++) = '"';
          *(zBuf++) = '\n';
        }
        if( psp->decllinenoslot && psp->decllinenoslot[0]==0 ){
          psp->decllinenoslot[0] = psp->tokenlineno;
        }
        memcpy(zBuf, zNew, nNew);
        zBuf += nNew;
        *zBuf = 0;
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Illegal argument to %%%s: %s",psp->declkeyword,x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_FALLBACK_ID:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( !ISUPPER(x[0]) ){
        ErrorMsg(psp->filename, psp->tokenlineno,
          "%%fallback argument \"%s\" should be a token", x);
        psp->errorcnt++;
      }else{
        struct symbol *sp = Symbol_new(x);
        if( psp->fallback==0 ){
          psp->fallback = sp;
        }else if( sp->fallback ){
          ErrorMsg(psp->filename, psp->tokenlineno,
            "More than one fallback assigned to token %s", x);
          psp->errorcnt++;
        }else{
          sp->fallback = psp->fallback;
          psp->gp->has_fallback = 1;
        }
      }
      break;
    case WAITING_FOR_TOKEN_NAME:
      /* Tokens do not have to be declared before use.  But they can be
      ** in order to control their assigned integer number.  The number for
      ** each token is assigned when it is first seen.  So by including
      **
      **     %token ONE TWO THREE.
      **
      ** early in the grammar file, that assigns small consecutive values
      ** to each of the tokens ONE TWO and THREE.
      */
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( !ISUPPER(x[0]) ){
        ErrorMsg(psp->filename, psp->tokenlineno,
          "%%token argument \"%s\" should be a token", x);
        psp->errorcnt++;
      }else{
        struct symbol *sp = Symbol_new(x);
        /* Lime-Letter-21: record each token name in the active
        ** %token group.  Multiple names on one source line
        ** (`%token A B C.`) all flow through here, one at a
        ** time, and append to the same group. */
        lime_token_group_append(psp->current_token_group, sp);
      }
      break;
    case WAITING_FOR_WILDCARD_ID:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( !ISUPPER(x[0]) ){
        ErrorMsg(psp->filename, psp->tokenlineno,
          "%%wildcard argument \"%s\" should be a token", x);
        psp->errorcnt++;
      }else{
        struct symbol *sp = Symbol_new(x);
        if( psp->gp->wildcard==0 ){
          psp->gp->wildcard = sp;
        }else{
          ErrorMsg(psp->filename, psp->tokenlineno,
            "Extra wildcard to token: %s", x);
          psp->errorcnt++;
        }
      }
      break;
    case WAITING_FOR_CLASS_ID:
      if( !ISLOWER(x[0]) ){
        ErrorMsg(psp->filename, psp->tokenlineno,
          "%%token_class must be followed by an identifier: %s", x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
     }else if( Symbol_find(x) ){
        ErrorMsg(psp->filename, psp->tokenlineno,
          "Symbol \"%s\" already used", x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }else{
        psp->tkclass = Symbol_new(x);
        psp->tkclass->type = MULTITERMINAL;
        psp->state = WAITING_FOR_CLASS_TOKEN;
      }
      break;
    case WAITING_FOR_CLASS_TOKEN:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( ISUPPER(x[0]) || ((x[0]=='|' || x[0]=='/') && ISUPPER(x[1])) ){
        struct symbol *msp = psp->tkclass;
        msp->nsubsym++;
        msp->subsym = (struct symbol **) lime_realloc(msp->subsym,
          sizeof(struct symbol*)*msp->nsubsym);
        if( !ISUPPER(x[0]) ) x++;
        msp->subsym[msp->nsubsym-1] = Symbol_new(x);
      }else{
        ErrorMsg(psp->filename, psp->tokenlineno,
          "%%token_class argument \"%s\" should be a token", x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_MODULE_REQUIRE:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        /* Parse: module_name [version_constraint] */
        /* The first token is the module name */
        struct module_dependency *dep;
        dep = (struct module_dependency*)lime_malloc(sizeof(*dep));
        dep->name = (char*)Strsafe(x);
        dep->version_constraint = 0;
        dep->next = psp->gp->dependencies;
        psp->gp->dependencies = dep;
        /* Store this dependency so we can add version constraint if present */
        psp->current_dependency = dep;
        psp->state = WAITING_FOR_MODULE_REQUIRE_VERSION;
      }
      break;
    case WAITING_FOR_MODULE_REQUIRE_VERSION:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
        psp->current_dependency = 0;
      }else if( x[0]=='%' ){
        /* New directive starting, end the require directive */
        psp->state = WAITING_FOR_DECL_KEYWORD;
        psp->current_dependency = 0;
      }else if( psp->current_dependency && !psp->current_dependency->version_constraint ){
        /* This is the version constraint */
        psp->current_dependency->version_constraint = (char*)Strsafe(x);
      }else{
        /* Unexpected token after version constraint */
        ErrorMsg(psp->filename, psp->tokenlineno,
          "Unexpected token in %%require: %s", x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_MODULE_EXPORT:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        /* Add this symbol to the exports list */
        struct exported_symbol *exp;
        exp = (struct exported_symbol*)lime_malloc(sizeof(*exp));
        exp->name = (char*)Strsafe(x);
        exp->next = psp->gp->exports;
        psp->gp->exports = exp;
      }
      break;
    case WAITING_FOR_MODULE_IMPORT:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( strcmp(x,"from")==0 ){
        /* Next token is the module name */
        psp->state = WAITING_FOR_MODULE_IMPORT_FROM;
      }else{
        /* Add this symbol to the imports list (module name will be added later) */
        struct imported_symbol *imp;
        imp = (struct imported_symbol*)lime_malloc(sizeof(*imp));
        imp->name = (char*)Strsafe(x);
        imp->from_module = 0; /* Will be set when we see 'from' */
        imp->next = psp->gp->imports;
        psp->gp->imports = imp;
      }
      break;
    case WAITING_FOR_MODULE_IMPORT_FROM:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        /* This is the module name - set it for all imports that don't have one */
        struct imported_symbol *imp;
        for(imp = psp->gp->imports; imp; imp = imp->next){
          if( imp->from_module==0 ){
            imp->from_module = (char*)Strsafe(x);
          }
        }
        psp->state = WAITING_FOR_MODULE_IMPORT_END;
      }
      break;
    case WAITING_FOR_MODULE_IMPORT_END:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        ErrorMsg(psp->filename, psp->tokenlineno,
          "Unexpected token after %%import from clause: %s", x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_ERROR_SYNC_TOKEN:
      if( x[0]=='.' ){
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( ISUPPER(x[0]) ){
        /* Add token to sync list */
        int n = psp->gp->n_error_sync_tokens;
        psp->gp->error_sync_tokens = (char**)realloc(
          psp->gp->error_sync_tokens, (n+1)*sizeof(char*));
        if( psp->gp->error_sync_tokens ){
          psp->gp->error_sync_tokens[n] = (char*)Strsafe(x);
          psp->gp->n_error_sync_tokens = n + 1;
        }
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Expected terminal symbol in %%error_sync but got \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_EXPECT_VALUE:
      if( ISDIGIT(x[0]) ){
        psp->gp->nexpect = atoi(x);
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( x[0]=='.' ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing argument to %%expect.");
        psp->errorcnt++;
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Expected integer argument to %%expect but got \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_FIRST_TOKEN_VALUE:
      /* %first_token N -- shift externally-visible terminal codes by N.
      ** Default 0 (Lemon convention).  Common values: 258 (Bison parity,
      ** reserves 0..127 for ASCII chars and 128..257 as a buffer).  The
      ** offset only affects the emitted #define values and the
      ** Parse() entry point's view of yymajor; internal action-table
      ** indices stay [1..nterminal-1]. */
      if( ISDIGIT(x[0]) ){
        int n = atoi(x);
        if( n<0 || n>32767 ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "%%first_token value must be 0..32767, got %d.", n);
          psp->errorcnt++;
          psp->state = WAITING_FOR_DECL_OR_RULE;
        }else{
          psp->gp->first_token = n;
          psp->state = WAITING_FOR_DECL_OR_RULE;
        }
      }else if( x[0]=='.' ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing argument to %%first_token.");
        psp->errorcnt++;
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Expected integer argument to %%first_token but got \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_AST_PREFIX_VALUE:
      if( ISALPHA(x[0]) ){
        psp->gp->ast_prefix = (char*)Strsafe(x);
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( x[0]=='.' ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing argument to %%ast_prefix.");
        psp->errorcnt++;
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Expected identifier for %%ast_prefix but got \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_AST_NODE_NAME:
      if( ISALPHA(x[0]) ){
        /* Store the node name temporarily in the pstate for the body parse */
        struct ast_node_def *nd;
        nd = (struct ast_node_def*)lime_malloc(sizeof(struct ast_node_def));
        MemoryCheck(nd);
        memset(nd, 0, sizeof(*nd));
        nd->name = (char*)Strsafe(x);
        nd->next = psp->gp->ast_nodes;
        psp->gp->ast_nodes = nd;
        psp->state = WAITING_FOR_AST_NODE_BODY;
      }else if( x[0]=='.' ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing node name for %%ast_node.");
        psp->errorcnt++;
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Expected identifier for %%ast_node name but got \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_AST_NODE_BODY:
      if( x[0]=='{' ){
        /* Parse the body: "type1 name1; type2 name2; ..." */
        const char *z = &x[1];
        struct ast_node_def *nd = psp->gp->ast_nodes; /* most recent */
        while( *z ){
          const char *typeStart;
          /* Skip whitespace */
          while( *z && ISSPACE(*z) ) z++;
          if( *z==0 || *z=='}' ) break;
          /* Read the type (may include '*' and multi-word like "struct Foo *") */
          typeStart = z;
          while( *z && *z!=';' && *z!='}' ){
            /* Scan forward looking for the last identifier before ';' or '}' */
            z++;
          }
          /* Back up from z to find the field name (last word before ';' or '}') */
          {
            const char *e = z;
            const char *ns, *ne;
            /* Skip trailing whitespace */
            while( e>typeStart && ISSPACE(e[-1]) ) e--;
            ne = e;
            /* Scan backwards for field name */
            while( e>typeStart && (ISALNUM(e[-1]) || e[-1]=='_') ) e--;
            ns = e;
            if( ns < ne ){
              /* Everything from typeStart to ns is the type (trim trailing space) */
              const char *te = ns;
              while( te>typeStart && ISSPACE(te[-1]) ) te--;
              if( te > typeStart ){
                struct ast_field *fld;
                char *typeBuf, *nameBuf;
                int typeLen = (int)(te - typeStart);
                int nameLen = (int)(ne - ns);
                fld = (struct ast_field*)lime_malloc(sizeof(struct ast_field));
                MemoryCheck(fld);
                typeBuf = (char*)lime_malloc(typeLen+1);
                MemoryCheck(typeBuf);
                memcpy(typeBuf, typeStart, typeLen);
                typeBuf[typeLen] = 0;
                nameBuf = (char*)lime_malloc(nameLen+1);
                MemoryCheck(nameBuf);
                memcpy(nameBuf, ns, nameLen);
                nameBuf[nameLen] = 0;
                fld->type = typeBuf;
                fld->name = nameBuf;
                fld->next = nd->fields;
                nd->fields = fld;
              }
            }
          }
          if( *z==';' ) z++;
        }
        /* Reverse the field list so fields appear in declaration order */
        {
          struct ast_field *prev = 0, *cur = nd->fields, *next;
          while( cur ){
            next = cur->next;
            cur->next = prev;
            prev = cur;
            cur = next;
          }
          nd->fields = prev;
        }
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( x[0]=='.' ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing body for %%ast_node. Expected '{...}'.");
        psp->errorcnt++;
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Expected '{' for %%ast_node body but got \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_AST_LIST_NAME:
      if( ISALPHA(x[0]) ){
        struct ast_node_def *nd;
        nd = (struct ast_node_def*)lime_malloc(sizeof(struct ast_node_def));
        MemoryCheck(nd);
        memset(nd, 0, sizeof(*nd));
        nd->name = (char*)Strsafe(x);
        nd->is_list = 1;
        nd->next = psp->gp->ast_nodes;
        psp->gp->ast_nodes = nd;
        psp->state = WAITING_FOR_AST_LIST_ELEMENT;
      }else if( x[0]=='.' ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing list name for %%ast_list.");
        psp->errorcnt++;
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Expected identifier for %%ast_list name but got \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_AST_LIST_ELEMENT:
      if( ISALPHA(x[0]) ){
        psp->gp->ast_nodes->element_type = (char*)Strsafe(x);
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else if( x[0]=='.' ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Missing element type for %%ast_list.");
        psp->errorcnt++;
        psp->state = WAITING_FOR_DECL_OR_RULE;
      }else{
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Expected element type for %%ast_list but got \"%s\".",x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }
      break;
    case WAITING_FOR_EXTENDS_PATH:
      /* v0.4.1: `%extends "path"` -- token text starts with `"`
      ** with the closing quote consumed by the tokenizer (the byte
      ** at the close-quote position has been NUL'd; *psp->tokenstart
      ** == '"' and the path follows).  Validate, resolve, recurse. */
      if( x[0]!='\"' || x[1]==0 ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "%%extends expects a quoted filename, got \"%s\"", x);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }else if( psp->extends_depth >= LIME_EXTENDS_MAX_DEPTH ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "%%extends nesting exceeds %d levels (cycle suspected)",
          LIME_EXTENDS_MAX_DEPTH);
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }else{
        const char *target = &x[1];
        const char *resolved =
          resolve_extends_path(psp->filename, target);
        if( resolved==0 ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "%%extends: cannot find file \"%s\" (searched relative "
            "to current file%s)",
            target,
            getenv("LIME_PATH")?" and LIME_PATH":"");
          psp->errorcnt++;
          psp->state = WAITING_FOR_DECL_OR_RULE;
        }else{
          /* Cycle check: refuse to follow a path that's already
          ** active on the extends stack (pointer-equal because
          ** Strsafe interns). */
          int i;
          int is_cycle = 0;
          for(i=0;i<psp->extends_depth;i++){
            if( psp->active_extends[i]==resolved ){ is_cycle=1; break; }
          }
          if( is_cycle ){
            ErrorMsg(psp->filename,psp->tokenlineno,
              "%%extends cycle: \"%s\" is already being parsed",
              resolved);
            psp->errorcnt++;
          }else{
            char *saved_filename = psp->filename;
            int saved_state = psp->state;
            /* Push and recurse.  Inside the recursive call the
            ** state machine starts fresh in WAITING_FOR_DECL_OR_RULE
            ** so the base file is parsed as a self-contained
            ** sequence of directives + rules. */
            psp->active_extends[psp->extends_depth++] = resolved;
            psp->state = WAITING_FOR_DECL_OR_RULE;
            parse_lime_file_recursive(psp, resolved, 0);
            psp->extends_depth--;
            psp->filename = saved_filename;
            psp->state = saved_state;
          }
          psp->state = WAITING_FOR_DECL_OR_RULE;
        }
      }
      break;
    case WAITING_FOR_OVERRIDE_TYPE_SYM:
      if( !ISALPHA(x[0]) ){
        ErrorMsg(psp->filename,psp->tokenlineno,
          "Symbol name missing after %%override_type keyword");
        psp->errorcnt++;
        psp->state = RESYNC_AFTER_DECL_ERROR;
      }else{
        struct symbol *sp = Symbol_find(x);
        if( sp==0 ){
          ErrorMsg(psp->filename,psp->tokenlineno,
            "%%override_type target symbol \"%s\" not declared "
            "(no %%type, no %%token, no rule reference)", x);
          psp->errorcnt++;
          psp->state = RESYNC_AFTER_DECL_ERROR;
        }else{
          /* v0.4.1: redirect to the standard WAITING_FOR_DECL_ARG
          ** path that reads the brace-delimited type body, exactly
          ** like %type does -- but PRESERVE the existing datatype
          ** for the warning we emit when the body lands.  We can't
          ** easily wedge into the post-arg-collected hook, so we
          ** stash the old value in declargslot's previous content
          ** by emitting the warning HERE, before the arg is
          ** collected.  The widening warning is intentionally
          ** noisy (PG-style ABI compat reminder). */
          if( sp->datatype && sp->datatype[0] ){
            fprintf(stderr,
              "%s:%d: warning: %%override_type widens %s from "
              "existing type; user-responsibility ABI compat\n",
              psp->filename, psp->tokenlineno, sp->name);
          }
          /* Free the existing datatype so WAITING_FOR_DECL_ARG
          ** stores the new one rather than appending. */
          if( sp->datatype ){
            /* sp->datatype was lime_realloc'd; safe to lime_free. */
            lime_free(sp->datatype);
            sp->datatype = 0;
          }
          psp->declargslot = &sp->datatype;
          psp->insertLineMacro = 0;
          psp->state = WAITING_FOR_DECL_ARG;
        }
      }
      break;
    case RESYNC_AFTER_RULE_ERROR:
/*      if( x[0]=='.' ) psp->state = WAITING_FOR_DECL_OR_RULE;
**      break; */
    case RESYNC_AFTER_DECL_ERROR:
      if( x[0]=='.' ) psp->state = WAITING_FOR_DECL_OR_RULE;
      if( x[0]=='%' ) psp->state = WAITING_FOR_DECL_KEYWORD;
      break;
  }
}

/* The text in the input is part of the argument to an %ifdef or %ifndef.
** Evaluate the text as a boolean expression.  Return true or false.
*/
static int eval_preprocessor_boolean(char *z, int lineno){
  int neg = 0;
  int res = 0;
  int okTerm = 1;
  int i;
  for(i=0; z[i]!=0; i++){
    if( ISSPACE(z[i]) ) continue;
    if( z[i]=='!' ){
      if( !okTerm ) goto pp_syntax_error;
      neg = !neg;
      continue;
    }
    if( z[i]=='|' && z[i+1]=='|' ){
      if( okTerm ) goto pp_syntax_error;
      if( res ) return 1;
      i++;
      okTerm = 1;
      continue;
    }
    if( z[i]=='&' && z[i+1]=='&' ){
      if( okTerm ) goto pp_syntax_error;
      if( !res ) return 0;
      i++;
      okTerm = 1;
      continue;
    }
    if( z[i]=='(' ){
      int k;
      int n = 1;
      if( !okTerm ) goto pp_syntax_error;
      for(k=i+1; z[k]; k++){
        if( z[k]==')' ){
          n--;
          if( n==0 ){
            z[k] = 0;
            res = eval_preprocessor_boolean(&z[i+1], -1);
            z[k] = ')';
            if( res<0 ){
              i = i-res;
              goto pp_syntax_error;
            }
            i = k;
            break;
          }
        }else if( z[k]=='(' ){
          n++;
        }else if( z[k]==0 ){
          i = k;
          goto pp_syntax_error;
        }
      }
      if( neg ){
        res = !res;
        neg = 0;
      }
      okTerm = 0;
      continue;
    }
    if( ISALPHA(z[i]) ){
      int j, k, n;
      if( !okTerm ) goto pp_syntax_error;
      for(k=i+1; ISALNUM(z[k]) || z[k]=='_'; k++){}
      n = k - i;
      res = 0;
      for(j=0; j<nDefine; j++){
        if( strncmp(azDefine[j],&z[i],n)==0 && azDefine[j][n]==0 ){
          if( !bDefineUsed[j] ){
            bDefineUsed[j] = 1;
            nDefineUsed++;
          }
          res = 1;
          break;
        }
      }
      i = k-1;
      if( neg ){
        res = !res;
        neg = 0;
      }
      okTerm = 0;
      continue;
    }
    goto pp_syntax_error;
  }
  return res;

pp_syntax_error:
  if( lineno>0 ){
    fprintf(stderr, "%%if syntax error on line %d.\n", lineno);
    fprintf(stderr, "  %.*s <-- syntax error here\n", i+1, z);
    exit(1);
  }else{
    return -(i+1);
  }
}

/* Desugar `%dialect NAME { ... }` blocks in place.
**
** v0.4.0 introduces `%dialect NAME { body }` as sugar for the
** equivalent `%ifdef dialect_NAME ... %endif` form, but with the
** body delimited by braces rather than a closing directive.  This
** pass runs BEFORE the existing %ifdef pass so the body, if
** included, can itself contain %ifdef / %ifndef / %if directives
** and be processed normally.
**
** Scanning rules:
**   - `%dialect` is recognized only at column zero, matching the
**     existing %ifdef convention (line-anchored).
**   - The NAME must satisfy [A-Za-z_][A-Za-z0-9_]* .
**   - The body is the brace-balanced region following the `{`.
**     The brace scan respects block and line comments and char
**     and string literals so a `}` inside any of those does not
**     close the block (mirrors Parse()'s C-action brace scan).
**   - Nested `%dialect` (a directive at column zero inside another
**     %dialect's body) is rejected with a clear error.  Users that
**     genuinely need conditional-on-multiple-dialects rules write
**     `%ifdef dialect_a %ifdef dialect_b ... %endif %endif`.
**
** Inclusion test:
**   - `dialect_NAME` is searched in azDefine[] -- the same array
**     %ifdef consults.  If defined, the surrounding `%dialect NAME`
**     opener and matching `}` are blanked to spaces; the body bytes
**     are left verbatim for downstream tokenization.
**   - If not defined, the entire region (directive + body + closing
**     brace) is blanked to spaces.
**
** Either branch preserves newlines exactly so error messages
** keep correct line numbers (no extra padding, no shifting).
**
** All edits are in place; the buffer length never changes, which
** lets the existing %ifdef pass run over the rewritten buffer
** without re-thinking offsets.  See docs/DIALECT.md.
*/
static void desugar_dialect_blocks(char *z, const char *filename){
  int i = 0;
  int lineno = 1;
  while( z[i] ){
    if( z[i]=='\n' ){ lineno++; i++; continue; }
    /* Recognize `%dialect` only at column zero. */
    if( z[i]!='%' || (i>0 && z[i-1]!='\n') ){ i++; continue; }
    if( strncmp(&z[i], "%dialect", 8)!=0
        || !ISSPACE((unsigned char)z[i+8]) ){
      i++;
      continue;
    }
    int dir_start = i;
    int dir_lineno = lineno;
    int j = i + 8;
    /* Skip horizontal whitespace before NAME.  A newline here is
    ** rejected -- the directive header is a single line. */
    while( z[j]==' ' || z[j]=='\t' ) j++;
    if( !ISALPHA((unsigned char)z[j]) && z[j]!='_' ){
      fprintf(stderr,
        "%s:%d: %%dialect: expected identifier after directive\n",
        filename, dir_lineno);
      exit(1);
    }
    int name_start = j;
    while( ISALNUM((unsigned char)z[j]) || z[j]=='_' ) j++;
    int name_end = j;
    int name_len = name_end - name_start;
    /* Allow newlines between NAME and `{` so users may format the
    ** opening brace on its own line. */
    while( z[j] && ISSPACE((unsigned char)z[j]) ){
      if( z[j]=='\n' ) lineno++;
      j++;
    }
    if( z[j] != '{' ){
      fprintf(stderr,
        "%s:%d: %%dialect %.*s: expected '{' after name\n",
        filename, dir_lineno, name_len, &z[name_start]);
      exit(1);
    }
    int brace_open = j;
    /* Walk the body, level-counted.
    ** Scanning rules:
    **   - block comments and line comments are skipped intact
    **   - char and string literals (with backslash escapes) are
    **     skipped intact
    **   - any `%dialect` at column zero inside the body is a hard
    **     error (no nested %dialect).
    ** Mirrors Parse()'s C-action brace scan. */
    int level = 1;
    int k = brace_open + 1;
    while( z[k] && level>0 ){
      char c = z[k];
      if( c=='\n' ){ lineno++; k++; continue; }
      if( c=='/' && z[k+1]=='*' ){
        k += 2;
        while( z[k] && !(z[k-1]=='*' && z[k]=='/') ){
          if( z[k]=='\n' ) lineno++;
          k++;
        }
        if( z[k] ) k++;
        continue;
      }
      if( c=='/' && z[k+1]=='/' ){
        k += 2;
        while( z[k] && z[k]!='\n' ) k++;
        continue;
      }
      if( c=='\'' || c=='\"' ){
        char start = c;
        char prev = 0;
        for(k++; z[k] && (z[k]!=start || prev=='\\'); k++){
          if( z[k]=='\n' ) lineno++;
          if( prev=='\\' ) prev = 0;
          else              prev = z[k];
        }
        if( z[k] ) k++;
        continue;
      }
      if( c=='%' && (k==0 || z[k-1]=='\n')
          && strncmp(&z[k], "%dialect", 8)==0
          && ISSPACE((unsigned char)z[k+8]) ){
        fprintf(stderr,
          "%s:%d: nested %%dialect inside %%dialect %.*s is not "
          "allowed; use explicit %%ifdef nesting instead\n",
          filename, lineno, name_len, &z[name_start]);
        exit(1);
      }
      if( c=='{' ){
        level++;
      }else if( c=='}' ){
        level--;
        if( level==0 ) break;
      }
      k++;
    }
    if( level!=0 ){
      fprintf(stderr,
        "%s:%d: unterminated %%dialect %.*s (missing '}')\n",
        filename, dir_lineno, name_len, &z[name_start]);
      exit(1);
    }
    int brace_close = k;
    /* Look up dialect_NAME in azDefine[]. */
    int defined = 0;
    int m;
    for(m=0; m<nDefine; m++){
      const char *macro = azDefine[m];
      if( strncmp(macro, "dialect_", 8)==0
          && (int)strlen(macro+8) == name_len
          && strncmp(macro+8, &z[name_start], (size_t)name_len)==0 ){
        if( !bDefineUsed[m] ){
          bDefineUsed[m] = 1;
          nDefineUsed++;
        }
        defined = 1;
        break;
      }
    }
    if( defined ){
      /* Blank the opener `%dialect NAME ... {` and the closing `}`,
      ** leaving the body verbatim. */
      int p;
      for(p=dir_start; p<=brace_open; p++){
        if( z[p]!='\n' ) z[p] = ' ';
      }
      if( z[brace_close]!='\n' ) z[brace_close] = ' ';
    }else{
      /* Blank the entire region; newlines preserved. */
      int p;
      for(p=dir_start; p<=brace_close; p++){
        if( z[p]!='\n' ) z[p] = ' ';
      }
    }
    i = brace_close + 1;
  }
}

/* Run the preprocessor over the input file text.  The global variables
** azDefine[0] through azDefine[nDefine-1] contains the names of all defined
** macros.  This routine looks for "%ifdef" and "%ifndef" and "%endif" and
** comments them out.  Text in between is also commented out as appropriate.
*/
static void preprocess_input(char *z){
  int i, j, k;
  int exclude = 0;
  int start = 0;
  int lineno = 1;
  int start_lineno = 1;
  for(i=0; z[i]; i++){
    if( z[i]=='\n' ) lineno++;
    if( z[i]!='%' || (i>0 && z[i-1]!='\n') ) continue;
    if( strncmp(&z[i],"%endif",6)==0 && ISSPACE(z[i+6]) ){
      if( exclude ){
        exclude--;
        if( exclude==0 ){
          for(j=start; j<i; j++) if( z[j]!='\n' ) z[j] = ' ';
        }
      }
      for(j=i; z[j] && z[j]!='\n'; j++) z[j] = ' ';
    }else if( strncmp(&z[i],"%else",5)==0 && ISSPACE(z[i+5]) ){
      if( exclude==1){
        exclude = 0;
        for(j=start; j<i; j++) if( z[j]!='\n' ) z[j] = ' ';
      }else if( exclude==0 ){
        exclude = 1;
        start = i;
        start_lineno = lineno;
      }
      for(j=i; z[j] && z[j]!='\n'; j++) z[j] = ' ';
    }else if( strncmp(&z[i],"%ifdef ",7)==0 
          || strncmp(&z[i],"%if ",4)==0
          || strncmp(&z[i],"%ifndef ",8)==0 ){
      if( exclude ){
        exclude++;
      }else{
        int isNot;
        int iBool;
        for(j=i; z[j] && !ISSPACE(z[j]); j++){}
        iBool = j;
        isNot = (j==i+7);
        while( z[j] && z[j]!='\n' ){ j++; }
        k = z[j];
        z[j] = 0;
        exclude = eval_preprocessor_boolean(&z[iBool], lineno);
        z[j] = k;
        if( !isNot ) exclude = !exclude;
        if( exclude ){
          start = i;
          start_lineno = lineno;
        }
      }
      for(j=i; z[j] && z[j]!='\n'; j++) z[j] = ' ';
    }
  }
  if( exclude ){
    fprintf(stderr,"unterminated %%ifdef starting on line %d\n", start_lineno);
    exit(1);
  }
}

/*
** v0.4.1: identity hash + equality for `%override` / `%remove` /
** diamond detection.
**
** Rule identity is `(LHS_name, RHS_symbol_sequence_in_order)`.  Alias
** names (LHS alias, per-RHS aliases) are intentionally excluded:
** `expr(A) ::= expr(B) PLUS expr(C).` and `expr(X) ::= expr(Y) PLUS
** expr(Z).` denote the SAME rule shape.  Action body and precedence
** mark are also excluded -- they're what `%override` REPLACES, so
** they must not participate in the matching key.  See
** docs/EXTENDS.md "Rule identity".
**
** Hash is djb2 mixed -- not cryptographic, just decent distribution
** so `find_rule_by_identity` can short-circuit on most negative
** lookups before the O(nrhs) name compare.
*/
static uint32_t lime_djb2(const char *s){
  uint32_t h = 5381;
  while( *s ){ h = ((h << 5) + h) ^ (uint32_t)(unsigned char)(*s++); }
  return h;
}
static uint32_t rule_identity_hash(struct rule *rp){
  uint32_t h;
  int i;
  if( rp==0 || rp->lhs==0 ) return 0;
  h = lime_djb2(rp->lhs->name);
  for(i=0; i<rp->nrhs; i++){
    /* MULTITERMINAL chains are stable -- subsym[0]'s name is the
    ** primary; we hash that for identity (compound terminals are
    ** an internal optimisation, not part of the user-visible
    ** rule shape). */
    struct symbol *sp = rp->rhs[i];
    const char *nm = sp ? sp->name : "?";
    h = (h * 1315423911u) ^ lime_djb2(nm);
  }
  return h;
}
static int rule_identity_eq(struct rule *a, struct rule *b){
  int i;
  if( a==b ) return 1;
  if( a==0 || b==0 ) return 0;
  if( a->lhs != b->lhs ) return 0;
  if( a->nrhs != b->nrhs ) return 0;
  for(i=0; i<a->nrhs; i++){
    /* Symbols are interned via Symbol_new(), so pointer-equal iff
    ** name-equal.  Cheaper than strcmp. */
    if( a->rhs[i] != b->rhs[i] ) return 0;
  }
  return 1;
}

/*
** Walk the linked rule list looking for a rule whose identity
** (LHS + RHS sequence) matches `probe`.  Returns NULL when no
** match.  Linear in the number of rules; for typical grammars
** (~1000 rules) this is cheap enough that we don't index --
** %extends-time mutations are rare relative to the parsing
** workload.  If a future grammar pushes this above noise, swap
** in a hash table keyed on rule_identity_hash().
*/
static struct rule *find_rule_by_identity(struct pstate *psp,
                                          struct rule *probe){
  struct rule *rp;
  for(rp = psp->firstrule; rp; rp = rp->next){
    if( rule_identity_eq(rp, probe) ) return rp;
  }
  return 0;
}

/*
** Resolve `%extends "target"` against the current file's directory.
** Returns a Strsafe-interned absolute-or-relative path on success,
** NULL on failure.  Search order:
**
**   1. Absolute path (target[0]=='/'): take as-is.
**   2. Relative to the current file's directory.
**   3. Each colon-separated entry in the LIME_PATH env var, in
**      order.
**
** Failure is signalled to the caller, which emits a diagnostic
** with the surrounding %extends line number.  No fallback to
** CWD: that has bitten enough Make-based build systems where
** CWD shifts as the project grows.  If the user wants CWD,
** they can set `LIME_PATH=.` explicitly.
*/
static const char *resolve_extends_path(const char *current_file,
                                        const char *target){
  struct stat st;
  char buf[4096];
  /* 1. Absolute path. */
  if( target[0]=='/' ){
    if( stat(target, &st)==0 ) return Strsafe(target);
    return 0;
  }
  /* 2. Relative to current file's directory. */
  if( current_file ){
    const char *slash = strrchr(current_file, '/');
    if( slash ){
      size_t dlen = (size_t)(slash - current_file + 1);
      if( dlen + strlen(target) + 1 < sizeof(buf) ){
        memcpy(buf, current_file, dlen);
        strcpy(buf + dlen, target);
        if( stat(buf, &st)==0 ) return Strsafe(buf);
      }
    }else{
      /* current_file has no `/` -- it lives in CWD.  Try target
      ** in CWD. */
      if( stat(target, &st)==0 ) return Strsafe(target);
    }
  }
  /* 3. LIME_PATH search. */
  {
    const char *path = getenv("LIME_PATH");
    if( path && *path ){
      const char *p = path;
      while( *p ){
        const char *colon = strchr(p, ':');
        size_t plen = colon ? (size_t)(colon - p) : strlen(p);
        if( plen > 0 && plen + 1 + strlen(target) + 1 < sizeof(buf) ){
          memcpy(buf, p, plen);
          buf[plen] = '/';
          strcpy(buf + plen + 1, target);
          if( stat(buf, &st)==0 ) return Strsafe(buf);
        }
        if( !colon ) break;
        p = colon + 1;
      }
    }
  }
  return 0;
}

/*
** v0.4.1: forward declarations are at the top of the file (search
** `parse_lime_file_recursive`) so commit_current_rule() can see
** them.  Definitions follow.
*/

/*
** v0.4.1: factored out of Parse().  Reads `filename`, runs the
** preprocessor passes, and tokenizes into psp->gp's accumulating
** rule/symbol/directive state.  Used both for the top-level user-
** invoked file (is_top_level==1) and for files brought in by
** `%extends` (is_top_level==0).
**
** Differences between the two:
**   - Top-level captures the file's leading comment block as
**     gp->header_comment (used by `lime -F` to round-trip the
**     copyright/IDENTIFICATION header).  Recursive calls do not
**     -- their leading comment, if any, lands inside the merged
**     grammar where it cannot meaningfully be re-emitted.
**   - Top-level expects psp->state to start at INITIALIZE so the
**     prevrule / firstrule / lastrule / nrule slots get reset.
**     Recursive calls pre-set state to WAITING_FOR_DECL_OR_RULE
**     so the existing accumulator survives.
**
** Memory: filebuf is freed at the end of this function.  Action
** bodies and identifiers within filebuf are interned via Strsafe()
** before that, so the rule / symbol tables outlive filebuf.
**
** State save/restore: the caller (parseonetoken) is responsible
** for saving and restoring psp->filename across the recursive
** call.  This function temporarily sets psp->filename to the new
** file but does not restore on return -- a recursive %extends
** would otherwise need to thread the saved filename through
** parse_lime_file_recursive's locals, which buys nothing.
*/
static void parse_lime_file_recursive(struct pstate *psp,
                                      const char *filename,
                                      int is_top_level)
{
  struct lime *gp = psp->gp;
  FILE *fp;
  char *filebuf;
  unsigned int filesize;
  int lineno;
  int c;
  char *cp, *nextcp;
  int startline = 0;
  char *comment_region_start = 0;
  char *body_start;

  fp = fopen(filename,"rb");
  if( fp==0 ){
    ErrorMsg(filename,0,"Can't open this file for reading.");
    gp->errorcnt++;
    psp->errorcnt++;
    return;
  }
  fseek(fp,0,2);
  filesize = ftell(fp);
  rewind(fp);
  filebuf = (char *)lime_malloc( filesize+1 );
  if( filesize>100000000 || filebuf==0 ){
    ErrorMsg(filename,0,"Input file too large.");
    lime_free(filebuf);
    gp->errorcnt++;
    psp->errorcnt++;
    fclose(fp);
    return;
  }
  if( fread(filebuf,1,filesize,fp)!=filesize ){
    ErrorMsg(filename,0,"Can't read in all %d bytes of this file.",
      filesize);
    lime_free(filebuf);
    gp->errorcnt++;
    psp->errorcnt++;
    fclose(fp);
    return;
  }
  fclose(fp);
  filebuf[filesize] = 0;

  /* Set the active filename for diagnostics + provenance.  Caller
  ** restores on return (in the %extends case). */
  psp->filename = (char *)filename;

  /* v0.4.0: %dialect desugaring runs before %ifdef preprocessing. */
  desugar_dialect_blocks(filebuf, filename);
  preprocess_input(filebuf);
  if( is_top_level && gp->printPreprocessed ){
    printf("%s\n", filebuf);
    lime_free(filebuf);
    return;
  }

  body_start = filebuf;
  if( is_top_level ){
    /* Capture file-level comments / whitespace BEFORE the first
    ** non-comment, non-whitespace byte (Lime-Letter-18). */
    char *hp = filebuf;
    while( *hp ){
      if( ISSPACE((unsigned char)*hp) ){ hp++; continue; }
      if( hp[0]=='/' && hp[1]=='/' ){
        hp+=2;
        while( *hp && *hp!='\n' ) hp++;
        continue;
      }
      if( hp[0]=='/' && hp[1]=='*' ){
        hp+=2;
        while( *hp && !(hp[-1]=='*' && hp[0]=='/') ) hp++;
        if( *hp ) hp++;
        continue;
      }
      break;
    }
    if( hp > filebuf ){
      size_t hlen = (size_t)(hp - filebuf);
      while( hlen>0 && ISSPACE((unsigned char)filebuf[hlen-1]) ) hlen--;
      if( hlen>0 ){
        gp->header_comment = (char*)lime_malloc(hlen + 1);
        if( gp->header_comment ){
          memcpy(gp->header_comment, filebuf, hlen);
          gp->header_comment[hlen] = 0;
        }
      }
      body_start = hp;
    }
  }

  /* Now scan the text of the input file */
  lineno = 1;
  for(cp=filebuf; cp<body_start; cp++){
    if( *cp=='\n' ) lineno++;
  }
  for(cp=body_start; (c= *cp)!=0; ){
    if( c=='\n' ) lineno++;
    if( ISSPACE(c) ){ cp++; continue; }
    if( c=='/' && cp[1]=='/' ){
      if( comment_region_start==0 ) comment_region_start = cp;
      cp+=2;
      while( (c= *cp)!=0 && c!='\n' ) cp++;
      continue;
    }
    if( c=='/' && cp[1]=='*' ){
      if( comment_region_start==0 ) comment_region_start = cp;
      cp+=2;
      if( (*cp)=='/' ) cp++;
      while( (c= *cp)!=0 && (c!='/' || cp[-1]!='*') ){
        if( c=='\n' ) lineno++;
        cp++;
      }
      if( c ) cp++;
      continue;
    }
    if( comment_region_start ){
      pstate_append_comment(psp, comment_region_start,
                            (size_t)(cp - comment_region_start));
      comment_region_start = 0;
    }
    psp->tokenstart = cp;
    psp->tokenlineno = lineno;
    if( c=='\"' ){
      cp++;
      while( (c= *cp)!=0 && c!='\"' ){
        if( c=='\n' ) lineno++;
        cp++;
      }
      if( c==0 ){
        ErrorMsg(filename,startline,
            "String starting on this line is not terminated before "
            "the end of the file.");
        psp->errorcnt++;
        nextcp = cp;
      }else{
        nextcp = cp+1;
      }
    }else if( c=='{' ){
      int level;
      cp++;
      for(level=1; (c= *cp)!=0 && (level>1 || c!='}'); cp++){
        if( c=='\n' ) lineno++;
        else if( c=='{' ) level++;
        else if( c=='}' ) level--;
        else if( c=='/' && cp[1]=='*' ){
          int prevc;
          cp = &cp[2];
          prevc = 0;
          while( (c= *cp)!=0 && (c!='/' || prevc!='*') ){
            if( c=='\n' ) lineno++;
            prevc = c;
            cp++;
          }
        }else if( c=='/' && cp[1]=='/' ){
          cp = &cp[2];
          while( (c= *cp)!=0 && c!='\n' ) cp++;
          if( c ) lineno++;
        }else if( c=='\'' || c=='\"' ){
          int startchar, prevc;
          startchar = c;
          prevc = 0;
          for(cp++; (c= *cp)!=0 && (c!=startchar || prevc=='\\'); cp++){
            if( c=='\n' ) lineno++;
            if( prevc=='\\' ) prevc = 0;
            else              prevc = c;
          }
        }
      }
      if( c==0 ){
        ErrorMsg(filename,psp->tokenlineno,
          "C code starting on this line is not terminated before "
          "the end of the file.");
        psp->errorcnt++;
        nextcp = cp;
      }else{
        nextcp = cp+1;
      }
    }else if( ISALNUM(c) ){
      while( (c= *cp)!=0 && (ISALNUM(c) || c=='_') ) cp++;
      nextcp = cp;
    }else if( c==':' && cp[1]==':' && cp[2]=='=' ){
      cp += 3;
      nextcp = cp;
    }else if( (c=='/' || c=='|') && ISALPHA(cp[1]) ){
      cp += 2;
      while( (c = *cp)!=0 && (ISALNUM(c) || c=='_') ) cp++;
      nextcp = cp;
    }else{
      cp++;
      nextcp = cp;
    }
    c = *cp;
    *cp = 0;
    parseonetoken(psp);
    *cp = (char)c;
    cp = nextcp;
  }
  if( comment_region_start ){
    pstate_append_comment(psp, comment_region_start,
                          (size_t)(cp - comment_region_start));
    comment_region_start = 0;
  }
  /* Trailing-comment slot is per-FILE in concept but per-PARSE in
  ** practice (the formatter only round-trips the top-level file).
  ** Drop the recursive call's trailing comments to avoid an
  ** earlier file's trailer clobbering the top-level file's. */
  if( psp->pending_comments ){
    char *pending = pstate_take_pending(psp);
    if( is_top_level ){
      gp->trailing_comment = pending;
    }else{
      lime_free(pending);
    }
  }
  if( psp->pending_directive_comment ){
    lime_free(psp->pending_directive_comment);
    psp->pending_directive_comment = 0;
  }
  if( psp->pending_rule_comment ){
    lime_free(psp->pending_rule_comment);
    psp->pending_rule_comment = 0;
  }
  lime_free(filebuf);
}

/* In spite of its name, this function is really a scanner.  It read
** in the entire input file (all at once) then tokenizes it.  Each
** token is passed to the function "parseonetoken" which builds all
** the appropriate data structures in the global state vector "gp".
*/
void Parse(struct lime *gp)
{
  struct pstate ps;

  memset(&ps, '\0', sizeof(ps));
  ps.gp = gp;
  ps.filename = gp->filename;
  ps.errorcnt = 0;
  ps.state = INITIALIZE;
  ps.extends_depth = 0;
  ps.pending_override = 0;
  ps.pending_remove = 0;

  parse_lime_file_recursive(&ps, gp->filename, 1);
  if( gp->printPreprocessed ){
    /* The recursive helper already printed; bail without
    ** finalising the rule list. */
    gp->errorcnt = ps.errorcnt;
    return;
  }

  gp->rule = ps.firstrule;
  gp->errorcnt = ps.errorcnt;

  /* v0.4.1: end-of-parse diamond conflict sweep.  Any rule whose
  ** conflict_pending flag is still set means two `%extends` paths
  ** disagreed on either an override body or an override-vs-remove
  ** decision and the user did not resolve the conflict in the
  ** derived file.  Per the locked design rules 4 and 8 in
  ** docs/EXTENDS.md, this is a hard error.  We print a separate
  ** diagnostic for each conflict so a single run reports them all. */
  {
    struct rule *rp;
    for(rp = gp->rule; rp; rp = rp->next){
      if( rp->conflict_pending ){
        ErrorMsg(rp->origin_file ? rp->origin_file : ps.filename,
          rp->origin_line,
          "diamond inheritance conflict: rule '%s' has conflicting "
          "%%override / %%remove from '%s' (line %d) and '%s'; add "
          "a %%override in the derived file to disambiguate",
          rp->lhs ? rp->lhs->name : "?",
          rp->conflict_file ? rp->conflict_file : "?",
          rp->conflict_line,
          rp->override_file ? rp->override_file : (rp->origin_file ? rp->origin_file : "?"));
        gp->errorcnt++;
      }
    }
  }

  /* Validate module metadata */
  if( gp->module_name && !gp->module_version ){
    ErrorMsg(ps.filename, 1,
      "%%module_name requires %%module_version");
    gp->errorcnt++;
  }
}
/*************************** From the file "plink.c" *********************/
/*
** Routines processing configuration follow-set propagation links
** in the LEMON parser generator.
*/
static struct plink *plink_freelist = 0;

/* Allocate a new plink */
struct plink *Plink_new(void){
  struct plink *newlink;

  if( plink_freelist==0 ){
    int i;
    int amt = 100;
    plink_freelist = (struct plink *)lime_calloc( amt, sizeof(struct plink) );
    if( plink_freelist==0 ){
      fprintf(stderr,
      "Unable to allocate memory for a new follow-set propagation link.\n");
      exit(1);
    }
    for(i=0; i<amt-1; i++) plink_freelist[i].next = &plink_freelist[i+1];
    plink_freelist[amt-1].next = 0;
  }
  newlink = plink_freelist;
  plink_freelist = plink_freelist->next;
  return newlink;
}

/* Add a plink to a plink list */
void Plink_add(struct plink **plpp, struct config *cfp)
{
  struct plink *newlink;
  newlink = Plink_new();
  newlink->next = *plpp;
  *plpp = newlink;
  newlink->cfp = cfp;
}

/* Transfer every plink on the list "from" to the list "to" */
void Plink_copy(struct plink **to, struct plink *from)
{
  struct plink *nextpl;
  while( from ){
    nextpl = from->next;
    from->next = *to;
    *to = from;
    from = nextpl;
  }
}

/* Delete every plink on the list */
void Plink_delete(struct plink *plp)
{
  struct plink *nextpl;

  while( plp ){
    nextpl = plp->next;
    plp->next = plink_freelist;
    plink_freelist = plp;
    plp = nextpl;
  }
}
/*********************** From the file "report.c" **************************/
/*
** Procedures for generating reports and tables in the LEMON parser generator.
*/

/* Generate a filename with the given suffix.
*/
PRIVATE char *file_makename(struct lime *lemp, const char *suffix)
{
  char *name;
  char *cp;
  char *filename = lemp->filename;
  int sz;

  if( outputDir ){
    cp = strrchr(filename, '/');
    if( cp ) filename = cp + 1;
  }
  sz = lemonStrlen(filename);
  sz += lemonStrlen(suffix);
  if( outputDir ) sz += lemonStrlen(outputDir) + 1;
  sz += 5;
  name = (char*)lime_malloc( sz );
  if( name==0 ){
    fprintf(stderr,"Can't allocate space for a filename.\n");
    exit(1);
  }
  name[0] = 0;
  if( outputDir ){
    lemon_strcpy(name, outputDir);
    lemon_strcat(name, "/");
  }
  lemon_strcat(name,filename);
  cp = strrchr(name,'.');
  if( cp ) *cp = 0;
  lemon_strcat(name,suffix);
  return name;
}

/* Open a file with a name based on the name of the input file,
** but with a different (specified) suffix, and return a pointer
** to the stream */
PRIVATE FILE *file_open(
  struct lime *lemp,
  const char *suffix,
  const char *mode
){
  FILE *fp;

  if( lemp->outname ) lime_free(lemp->outname);
  lemp->outname = file_makename(lemp, suffix);
  fp = fopen(lemp->outname,mode);
  if( fp==0 && *mode=='w' ){
    fprintf(stderr,"Can't open file \"%s\".\n",lemp->outname);
    lemp->errorcnt++;
    return 0;
  }
  return fp;
}

/* Print the text of a rule
*/
void rule_print(FILE *out, struct rule *rp){
  int i, j;
  fprintf(out, "%s",rp->lhs->name);
  /*    if( rp->lhsalias ) fprintf(out,"(%s)",rp->lhsalias); */
  fprintf(out," ::=");
  for(i=0; i<rp->nrhs; i++){
    struct symbol *sp = rp->rhs[i];
    if( sp->type==MULTITERMINAL ){
      fprintf(out," %s", sp->subsym[0]->name);
      for(j=1; j<sp->nsubsym; j++){
        fprintf(out,"|%s", sp->subsym[j]->name);
      }
    }else{
      fprintf(out," %s", sp->name);
    }
    /* if( rp->rhsalias[i] ) fprintf(out,"(%s)",rp->rhsalias[i]); */
  }
}

/* Duplicate the input file without comments and without actions
** on rules */
void Reprint(struct lime *lemp)
{
  struct rule *rp;
  struct symbol *sp;
  int i, j, maxlen, len, ncolumns, skip;
  printf("// Reprint of input file \"%s\".\n// Symbols:\n",lemp->filename);
  maxlen = 10;
  for(i=0; i<lemp->nsymbol; i++){
    sp = lemp->symbols[i];
    len = lemonStrlen(sp->name);
    if( len>maxlen ) maxlen = len;
  }
  ncolumns = 76/(maxlen+5);
  if( ncolumns<1 ) ncolumns = 1;
  skip = (lemp->nsymbol + ncolumns - 1)/ncolumns;
  for(i=0; i<skip; i++){
    printf("//");
    for(j=i; j<lemp->nsymbol; j+=skip){
      sp = lemp->symbols[j];
      assert( sp->index==j );
      printf(" %3d %-*.*s",j,maxlen,maxlen,sp->name);
    }
    printf("\n");
  }
  for(rp=lemp->rule; rp; rp=rp->next){
    rule_print(stdout, rp);
    printf(".");
    if( rp->precsym ) printf(" [%s]",rp->precsym->name);
    /* if( rp->code ) printf("\n    %s",rp->code); */
    printf("\n");
  }
}

/* Print a single rule.
*/
void RulePrint(FILE *fp, struct rule *rp, int iCursor){
  struct symbol *sp;
  int i, j;
  fprintf(fp,"%s ::=",rp->lhs->name);
  for(i=0; i<=rp->nrhs; i++){
    if( i==iCursor ) fprintf(fp," *");
    if( i==rp->nrhs ) break;
    sp = rp->rhs[i];
    if( sp->type==MULTITERMINAL ){
      fprintf(fp," %s", sp->subsym[0]->name);
      for(j=1; j<sp->nsubsym; j++){
        fprintf(fp,"|%s",sp->subsym[j]->name);
      }
    }else{
      fprintf(fp," %s", sp->name);
    }
  }
}

/* Print the rule for a configuration.
*/
void ConfigPrint(FILE *fp, struct config *cfp){
  RulePrint(fp, cfp->rp, cfp->dot);
}

/* #define TEST */
#if 0
/* Print a set */
PRIVATE void SetPrint(out,set,lemp)
FILE *out;
char *set;
struct lime *lemp;
{
  int i;
  char *spacer;
  spacer = "";
  fprintf(out,"%12s[","");
  for(i=0; i<lemp->nterminal; i++){
    if( SetFind(set,i) ){
      fprintf(out,"%s%s",spacer,lemp->symbols[i]->name);
      spacer = " ";
    }
  }
  fprintf(out,"]\n");
}

/* Print a plink chain */
PRIVATE void PlinkPrint(out,plp,tag)
FILE *out;
struct plink *plp;
char *tag;
{
  while( plp ){
    fprintf(out,"%12s%s (state %2d) ","",tag,plp->cfp->stp->statenum);
    ConfigPrint(out,plp->cfp);
    fprintf(out,"\n");
    plp = plp->next;
  }
}
#endif

/* Print an action to the given file descriptor.  Return FALSE if
** nothing was actually printed.
*/
int PrintAction(
  struct action *ap,          /* The action to print */
  FILE *fp,                   /* Print the action here */
  int indent                  /* Indent by this amount */
){
  int result = 1;
  switch( ap->type ){
    case SHIFT: {
      struct state *stp = ap->x.stp;
      fprintf(fp,"%*s shift        %-7d",indent,ap->sp->name,stp->statenum);
      break;
    }
    case REDUCE: {
      struct rule *rp = ap->x.rp;
      fprintf(fp,"%*s reduce       %-7d",indent,ap->sp->name,rp->iRule);
      RulePrint(fp, rp, -1);
      break;
    }
    case SHIFTREDUCE: {
      struct rule *rp = ap->x.rp;
      fprintf(fp,"%*s shift-reduce %-7d",indent,ap->sp->name,rp->iRule);
      RulePrint(fp, rp, -1);
      break;
    }
    case ACCEPT:
      fprintf(fp,"%*s accept",indent,ap->sp->name);
      break;
    case ERROR:
      fprintf(fp,"%*s error",indent,ap->sp->name);
      break;
    case SRCONFLICT:
      fprintf(fp,"%*s reduce       %-7d ** Shift/Reduce conflict **",
        indent,ap->sp->name,ap->x.rp->iRule);
      fprintf(fp,"\n%*s              on lookahead: %s",indent,"",ap->sp->name);
      fprintf(fp,"\n%*s              reduce rule:  ",indent,"");
      RulePrint(fp, ap->x.rp, -1);
      fprintf(fp,"\n%*s              consider: add precedence to token '%s'",
        indent,"",ap->sp->name);
      if( ap->x.rp->precsym==0 ){
        fprintf(fp,
          "\n%*s              or: use %%left/%%right/%%nonassoc for '%s'",
          indent,"",ap->sp->name);
      }
      break;
    case RRCONFLICT:
      fprintf(fp,"%*s reduce       %-7d ** Reduce/Reduce conflict **",
        indent,ap->sp->name,ap->x.rp->iRule);
      fprintf(fp,"\n%*s              on lookahead: %s",indent,"",ap->sp->name);
      fprintf(fp,"\n%*s              reduce rule:  ",indent,"");
      RulePrint(fp, ap->x.rp, -1);
      fprintf(fp,"\n%*s              consider: use %%fallback or "
        "refactor grammar to eliminate ambiguity",indent,"");
      break;
    case SSCONFLICT:
      fprintf(fp,"%*s shift        %-7d ** Shift/Shift conflict **",
        indent,ap->sp->name,ap->x.stp->statenum);
      fprintf(fp,"\n%*s              on lookahead: %s",indent,"",ap->sp->name);
      break;
    case SH_RESOLVED:
      if( showPrecedenceConflict ){
        fprintf(fp,"%*s shift        %-7d -- dropped by precedence",
                indent,ap->sp->name,ap->x.stp->statenum);
      }else{
        result = 0;
      }
      break;
    case RD_RESOLVED:
      if( showPrecedenceConflict ){
        fprintf(fp,"%*s reduce %-7d -- dropped by precedence",
                indent,ap->sp->name,ap->x.rp->iRule);
      }else{
        result = 0;
      }
      break;
    case NOT_USED:
      result = 0;
      break;
  }
  if( result && ap->spOpt ){
    fprintf(fp,"  /* because %s==%s */", ap->sp->name, ap->spOpt->name);
  }
  return result;
}

/* Generate the "*.out" log file */
void ReportOutput(struct lime *lemp)
{
  int i, n;
  struct state *stp;
  struct config *cfp;
  struct action *ap;
  struct rule *rp;
  FILE *fp;

  fp = file_open(lemp,".out","wb");
  if( fp==0 ) return;
  for(i=0; i<lemp->nxstate; i++){
    stp = lemp->sorted[i];
    fprintf(fp,"State %d:\n",stp->statenum);
    if( lemp->basisflag ) cfp=stp->bp;
    else                  cfp=stp->cfp;
    while( cfp ){
      char buf[20];
      if( cfp->dot==cfp->rp->nrhs ){
        lemon_sprintf(buf,"(%d)",cfp->rp->iRule);
        fprintf(fp,"    %5s ",buf);
      }else{
        fprintf(fp,"          ");
      }
      ConfigPrint(fp,cfp);
      fprintf(fp,"\n");
#if 0
      SetPrint(fp,cfp->fws,lemp);
      PlinkPrint(fp,cfp->fplp,"To  ");
      PlinkPrint(fp,cfp->bplp,"From");
#endif
      if( lemp->basisflag ) cfp=cfp->bp;
      else                  cfp=cfp->next;
    }
    fprintf(fp,"\n");
    for(ap=stp->ap; ap; ap=ap->next){
      if( PrintAction(ap,fp,30) ) fprintf(fp,"\n");
    }

    /* Concise per-state audit summary, intended for mechanical diff
    ** against another generator's report (e.g. bison's .output) when
    ** error-position parity matters.  Lists exactly:
    **   * which terminals shift / shift-reduce / reduce-by-rule from
    **     this state (the "accept set");
    **   * which non-terminals goto from this state;
    **   * the default reduce rule, if any (-1 means there is none
    **     and a lookahead not in the accept set fires %syntax_error). */
    {
      int n_terms = 0, n_nterms = 0;
      fprintf(fp, "  Accept terminals:    ");
      for(ap=stp->ap; ap; ap=ap->next){
        if( ap->sp->type!=TERMINAL && ap->sp->type!=MULTITERMINAL ) continue;
        if( ap->type==SHIFT || ap->type==SHIFTREDUCE
            || ap->type==REDUCE || ap->type==ACCEPT ){
          fprintf(fp, " %s", ap->sp->name);
          n_terms++;
        }
      }
      if( n_terms==0 ) fprintf(fp, " (none)");
      fprintf(fp, "\n");
      fprintf(fp, "  Goto non-terminals:  ");
      for(ap=stp->ap; ap; ap=ap->next){
        if( ap->sp->type!=NONTERMINAL ) continue;
        if( ap->type==SHIFT || ap->type==SHIFTREDUCE
            || ap->type==REDUCE || ap->type==ACCEPT ){
          fprintf(fp, " %s", ap->sp->name);
          n_nterms++;
        }
      }
      if( n_nterms==0 ) fprintf(fp, " (none)");
      fprintf(fp, "\n");
      fprintf(fp, "  Default reduce rule: ");
      if( stp->iDfltReduce<0 ){
        fprintf(fp, "none (lookahead not in accept set fires %%syntax_error)\n");
      }else{
        fprintf(fp, "%d\n", stp->iDfltReduce);
      }
    }
    fprintf(fp,"\n");
  }
  fprintf(fp, "----------------------------------------------------\n");
  fprintf(fp, "Symbols:\n");
  fprintf(fp, "The first-set of non-terminals is shown after the name.\n\n");
  for(i=0; i<lemp->nsymbol; i++){
    int j;
    struct symbol *sp;

    sp = lemp->symbols[i];
    fprintf(fp, "  %3d: %s", i, sp->name);
    if( sp->type==NONTERMINAL ){
      fprintf(fp, ":");
      if( sp->lambda ){
        fprintf(fp, " <lambda>");
      }
      for(j=0; j<lemp->nterminal; j++){
        if( sp->firstset && SetFind(sp->firstset, j) ){
          fprintf(fp, " %s", lemp->symbols[j]->name);
        }
      }
    }
    if( sp->prec>=0 ) fprintf(fp," (precedence=%d)", sp->prec);
    fprintf(fp, "\n");
  }
  fprintf(fp, "----------------------------------------------------\n");
  fprintf(fp, "Syntax-only Symbols:\n");
  fprintf(fp, "The following symbols never carry semantic content.\n\n");
  for(i=n=0; i<lemp->nsymbol; i++){
    int w;
    struct symbol *sp = lemp->symbols[i];
    if( sp->bContent ) continue;
    w = (int)strlen(sp->name);
    if( n>0 && n+w>75 ){
      fprintf(fp,"\n");
      n = 0;
    }
    if( n>0 ){
      fprintf(fp, " ");
      n++;
    }
    fprintf(fp, "%s", sp->name);
    n += w;
  }
  if( n>0 ) fprintf(fp, "\n");
  fprintf(fp, "----------------------------------------------------\n");
  fprintf(fp, "Rules:\n");
  for(rp=lemp->rule; rp; rp=rp->next){
    fprintf(fp, "%4d: ", rp->iRule);
    rule_print(fp, rp);
    fprintf(fp,".");
    if( rp->precsym ){
      fprintf(fp," [%s precedence=%d]",
              rp->precsym->name, rp->precsym->prec);
    }
    fprintf(fp,"\n");
  }

  /* Conflict summary section -- collects all conflicts in one place */
  if( lemp->nconflict > 0 ){
    int nc = 0;
    fprintf(fp, "----------------------------------------------------\n");
    fprintf(fp, "Conflict Summary (%d conflict%s):\n\n",
            lemp->nconflict, lemp->nconflict==1 ? "" : "s");
    for(i=0; i<lemp->nxstate; i++){
      struct action *ap2;
      stp = lemp->sorted[i];
      for(ap2=stp->ap; ap2; ap2=ap2->next){
        if( ap2->type==SRCONFLICT || ap2->type==RRCONFLICT
            || ap2->type==SSCONFLICT ){
          nc++;
          fprintf(fp, "  %d. State %d, lookahead '%s': ",
                  nc, stp->statenum, ap2->sp->name);
          if( ap2->type==SRCONFLICT ){
            fprintf(fp, "shift/reduce conflict (rule %d)\n",
                    ap2->x.rp->iRule);
            fprintf(fp, "     reduce rule: ");
            RulePrint(fp, ap2->x.rp, -1);
            fprintf(fp, "\n");
          }else if( ap2->type==RRCONFLICT ){
            fprintf(fp, "reduce/reduce conflict (rule %d)\n",
                    ap2->x.rp->iRule);
            fprintf(fp, "     reduce rule: ");
            RulePrint(fp, ap2->x.rp, -1);
            fprintf(fp, "\n");
          }else{
            fprintf(fp, "shift/shift conflict (state %d)\n",
                    ap2->x.stp->statenum);
          }
        }
      }
    }
    if( lemp->nexpect >= 0 ){
      fprintf(fp, "\n  %%expect %d declared", lemp->nexpect);
      if( lemp->nconflict == lemp->nexpect ){
        fprintf(fp, " (matches)\n");
      }else{
        fprintf(fp, " (MISMATCH: found %d)\n", lemp->nconflict);
      }
    }
    fprintf(fp, "\n");
  }

  fclose(fp);
  return;
}

/* Search for the file "name" which is in the same directory as
** the executable */
PRIVATE char *pathsearch(char *argv0, char *name, int modemask)
{
  const char *pathlist;
  char *pathbufptr = 0;
  char *pathbuf = 0;
  char *path,*cp;
  char c;

#ifdef __WIN32__
  cp = strrchr(argv0,'\\');
#else
  cp = strrchr(argv0,'/');
#endif
  if( cp ){
    c = *cp;
    *cp = 0;
    path = (char *)lime_malloc( lemonStrlen(argv0) + lemonStrlen(name) + 2 );
    if( path ) lemon_sprintf(path,"%s/%s",argv0,name);
    *cp = c;
  }else{
    pathlist = getenv("PATH");
    if( pathlist==0 ) pathlist = ".:/bin:/usr/bin";
    pathbuf = (char *) lime_malloc( lemonStrlen(pathlist) + 1 );
    path = (char *)lime_malloc( lemonStrlen(pathlist)+lemonStrlen(name)+2 );
    if( (pathbuf != 0) && (path!=0) ){
      pathbufptr = pathbuf;
      lemon_strcpy(pathbuf, pathlist);
      while( *pathbuf ){
        cp = strchr(pathbuf,':');
        if( cp==0 ) cp = &pathbuf[lemonStrlen(pathbuf)];
        c = *cp;
        *cp = 0;
        lemon_sprintf(path,"%s/%s",pathbuf,name);
        *cp = c;
        if( c==0 ) pathbuf[0] = 0;
        else pathbuf = &cp[1];
        if( access(path,modemask)==0 ) break;
      }
    }
    lime_free(pathbufptr);
  }
  return path;
}

/* Given an action, compute the integer value for that action
** which is to be put in the action table of the generated machine.
** Return negative if no action should be generated.
*/
PRIVATE int compute_action(struct lime *lemp, struct action *ap)
{
  int act;
  switch( ap->type ){
    case SHIFT:  act = ap->x.stp->statenum;                        break;
    case SHIFTREDUCE: {
      /* Since a SHIFT is inherent after a prior REDUCE, convert any
      ** SHIFTREDUCE action with a nonterminal on the LHS into a simple
      ** REDUCE action: */
      if( ap->sp->index>=lemp->nterminal
       && (lemp->errsym==0 || ap->sp->index!=lemp->errsym->index)
      ){
        act = lemp->minReduce + ap->x.rp->iRule;
      }else{
        act = lemp->minShiftReduce + ap->x.rp->iRule;
      }
      break;
    }
    case REDUCE: act = lemp->minReduce + ap->x.rp->iRule;          break;
    case ERROR:  act = lemp->errAction;                            break;
    case ACCEPT: act = lemp->accAction;                            break;
    default:     act = -1; break;
  }
  return act;
}

#define LINESIZE 10000
/* The next cluster of routines are for reading the template file
** and writing the results to the generated parser */
/* The first function transfers data from "in" to "out" until
** a line is seen which begins with "%%".  The line number is
** tracked.
**
** if name!=0, then any word that begin with "Parse" is changed to
** begin with *name instead.
*/
PRIVATE void tplt_xfer(char *name, FILE *in, FILE *out, int *lineno)
{
  int i, iStart;
  char line[LINESIZE];
  while( fgets(line,LINESIZE,in) && (line[0]!='%' || line[1]!='%') ){
    (*lineno)++;
    iStart = 0;
    if( name ){
      for(i=0; line[i]; i++){
        if( line[i]=='P' && strncmp(&line[i],"Parse",5)==0
          && (i==0 || !ISALPHA(line[i-1]))
        ){
          if( i>iStart ) fprintf(out,"%.*s",i-iStart,&line[iStart]);
          fprintf(out,"%s",name);
          i += 4;
          iStart = i+1;
        }
      }
    }
    fprintf(out,"%s",&line[iStart]);
  }
}

/* Skip forward past the header of the template file to the first "%%"
*/
PRIVATE void tplt_skip_header(FILE *in){
  char line[LINESIZE];
  while( fgets(line,LINESIZE,in) && (line[0]!='%' || line[1]!='%') ){}
}

/* The next function finds the template file and opens it, returning
** a pointer to the opened file. */
PRIVATE FILE *tplt_open(struct lime *lemp)
{
  static char templatename[] = "lempar.c";
  char buf[1000];
  FILE *in;
  char *tpltname;
  char *toFree = 0;
  char *cp;

  /* first, see if user specified a template filename on the command line. */
  if (user_templatename != 0) {
    if( access(user_templatename,004)==-1 ){
      fprintf(stderr,"Can't find the parser driver template file \"%s\".\n",
        user_templatename);
      lemp->errorcnt++;
      return 0;
    }
    in = fopen(user_templatename,"rb");
    if( in==0 ){
      fprintf(stderr,"Can't open the template file \"%s\".\n",
              user_templatename);
      lemp->errorcnt++;
      return 0;
    }
    return in;
  }

  cp = strrchr(lemp->filename,'.');
  if( cp ){
    lemon_sprintf(buf,"%.*s.lt",(int)(cp-lemp->filename),lemp->filename);
  }else{
    lemon_sprintf(buf,"%s.lt",lemp->filename);
  }
  if( access(buf,004)==0 ){
    tpltname = buf;
  }else if( access(templatename,004)==0 ){
    tpltname = templatename;
  }else{
    toFree = tpltname = pathsearch(lemp->argv[0],templatename,0);
  }
  if( tpltname==0 ){
    fprintf(stderr,"Can't find the parser driver template file \"%s\".\n",
    templatename);
    lemp->errorcnt++;
    return 0;
  }
  in = fopen(tpltname,"rb");
  if( in==0 ){
    fprintf(stderr,"Can't open the template file \"%s\".\n",tpltname);
    lemp->errorcnt++;
  }
  lime_free(toFree);
  return in;
}

/* Print a #line directive line to the output file. */
PRIVATE void tplt_linedir(FILE *out, int lineno, char *filename)
{
  fprintf(out,"#line %d \"",lineno);
  while( *filename ){
    if( *filename == '\\' ) putc('\\',out);
    putc(*filename,out);
    filename++;
  }
  fprintf(out,"\"\n");
  fflush(out);
}

/* Print a string to the file and keep the linenumber up to date */
PRIVATE void tplt_print(FILE *out, struct lime *lemp, char *str, int *lineno)
{
  const char *start;
  if( str==0 ) return;
  fflush(out);
  start = str;
  while( *str ){
    putc(*str,out);
    if( *str=='\n' ) (*lineno)++;
    str++;
  }
  /* Append a newline only if the string was non-empty AND the last
  ** character we wrote was not already '\n'.  Reading str[-1] when
  ** str==start would be an out-of-bounds read of the global string
  ** literal, caught by AddressSanitizer.  See ASan report:
  **   global-buffer-overflow ... 1 bytes before global variable '*.LC5'
  ** when str is the empty string "". */
  if( str>start && str[-1]!='\n' ){
    putc('\n',out);
    (*lineno)++;
  }
  if (!lemp->nolinenosflag) {
    (*lineno)++; tplt_linedir(out,*lineno,lemp->outname);
  }
  fflush(out);
  return;
}

/*
** The following routine emits code for the destructor for the
** symbol sp
*/
void emit_destructor_code(
  FILE *out,
  struct symbol *sp,
  struct lime *lemp,
  int *lineno
){
 char *cp = 0;

 if( sp->type==TERMINAL ){
   cp = lemp->tokendest;
   if( cp==0 ) return;
   fprintf(out,"{\n"); (*lineno)++;
 }else if( sp->destructor ){
   cp = sp->destructor;
   fprintf(out,"{\n"); (*lineno)++;
   if( !lemp->nolinenosflag ){
     (*lineno)++;
     tplt_linedir(out,sp->destLineno,lemp->filename);
   }
 }else if( lemp->vardest ){
   cp = lemp->vardest;
   if( cp==0 ) return;
   fprintf(out,"{\n"); (*lineno)++;
 }else{
   assert( 0 );  /* Cannot happen */
 }
 for(; *cp; cp++){
   if( *cp=='$' && cp[1]=='$' ){
     fprintf(out,"(yypminor->yy%d)",sp->dtnum);
     cp++;
     continue;
   }
   if( *cp=='\n' ) (*lineno)++;
   fputc(*cp,out);
 }
 fprintf(out,"\n"); (*lineno)++;
 if (!lemp->nolinenosflag) {
   (*lineno)++; tplt_linedir(out,*lineno,lemp->outname);
 }
 fprintf(out,"}\n"); (*lineno)++;
 return;
}

/*
** Return TRUE (non-zero) if the given symbol has a destructor.
*/
int has_destructor(struct symbol *sp, struct lime *lemp)
{
  int ret;
  if( sp->type==TERMINAL ){
    ret = lemp->tokendest!=0;
  }else{
    ret = lemp->vardest!=0 || sp->destructor!=0;
  }
  return ret;
}

/*
** Append text to a dynamically allocated string.  If zText is 0 then
** reset the string to be empty again.  Always return the complete text
** of the string (which is overwritten with each call).
**
** n bytes of zText are stored.  If n==0 then all of zText up to the first
** \000 terminator is stored.  zText can contain up to two instances of
** %d.  The values of p1 and p2 are written into the first and second
** %d.
**
** If n==-1, then the previous character is overwritten.
*/
PRIVATE char *append_str(const char *zText, int n, int p1, int p2){
  static char empty[1] = { 0 };
  static char *z = 0;
  static int alloced = 0;
  static int used = 0;
  int c;
  char zInt[40];
  if( zText==0 ){
    if( used==0 && z!=0 ) z[0] = 0;
    used = 0;
    return z;
  }
  if( n<=0 ){
    if( n<0 ){
      used += n;
      assert( used>=0 );
    }
    n = lemonStrlen(zText);
  }
  if( (int) (n+sizeof(zInt)*2+used) >= alloced ){
    alloced = n + sizeof(zInt)*2 + used + 200;
    z = (char *) lime_realloc(z,  alloced);
  }
  if( z==0 ) return empty;
  while( n-- > 0 ){
    c = *(zText++);
    if( c=='%' && n>0 && zText[0]=='d' ){
      lemon_sprintf(zInt, "%d", p1);
      p1 = p2;
      lemon_strcpy(&z[used], zInt);
      used += lemonStrlen(&z[used]);
      zText++;
      n--;
    }else{
      z[used++] = (char)c;
    }
  }
  z[used] = 0;
  return z;
}

/*
** Write and transform the rp->code string so that symbols are expanded.
** Populate the rp->codePrefix and rp->codeSuffix strings, as appropriate.
**
** Return 1 if the expanded code requires that "yylhsminor" local variable
** to be defined.
*/
/* P0-NEW-11: lexical states for the action-body byte walk.  The
** substitution pass rewrites bare alphabetic identifiers that match
** an LHS or RHS alias into stack-slot references.  Without lexical
** awareness the walk happily rewrites tokens inside string literals,
** char literals, and comments -- a real bug surfaced by PG's
** contrib/cube port (`errdetail("A cube cannot have more than %d
** dimensions.")` with LHS alias `(A)` mangled to
** `errdetail("yylhsminor.yy0 cube ...")`).  Tracking these states
** through the walk keeps substitution code-only.
*/
#define LIME_TC_CODE          0
#define LIME_TC_STRING        1  /* inside "..." */
#define LIME_TC_CHAR          2  /* inside '...' */
#define LIME_TC_LINE_COMMENT  3  /* inside // ... \n */
#define LIME_TC_BLOCK_COMMENT 4  /* inside / * ... * / */

PRIVATE int translate_code(struct lime *lemp, struct rule *rp){
  char *cp, *xp;
  int i;
  int rc = 0;            /* True if yylhsminor is used */
  int dontUseRhs0 = 0;   /* If true, use of left-most RHS label is illegal */
  const char *zSkip = 0; /* The zOvwrt comment within rp->code, or NULL */
  char lhsused = 0;      /* True if the LHS element has been used */
  char lhsdirect;        /* True if LHS writes directly into stack */
  char used[MAXRHS];     /* True for each RHS element which is used */
  char zLhs[50];         /* Convert the LHS symbol into this string */
  char zOvwrt[900];      /* Comment that to allow LHS to overwrite RHS */
  int  tc_state = LIME_TC_CODE; /* P0-NEW-11: body walk lexical state */

  for(i=0; i<rp->nrhs; i++) used[i] = 0;
  lhsused = 0;

  if( rp->code==0 ){
    static char newlinestr[2] = { '\n', '\0' };
    rp->code = newlinestr;
    rp->line = rp->ruleline;
    rp->noCode = 1;
  }else{
    rp->noCode = 0;
  }


  if( rp->nrhs==0 ){
    /* If there are no RHS symbols, then writing directly to the LHS is ok */
    lhsdirect = 1;
  }else if( rp->rhsalias[0]==0 ){
    /* The left-most RHS symbol has no value.  LHS direct is ok.  But
    ** we have to call the destructor on the RHS symbol first. */
    lhsdirect = 1;
    if( has_destructor(rp->rhs[0],lemp) ){
      append_str(0,0,0,0);
      append_str("  yy_destructor(yypParser,%d,&yymsp[%d].minor);\n", 0,
                 rp->rhs[0]->index,1-rp->nrhs);
      rp->codePrefix = Strsafe(append_str(0,0,0,0));
      rp->noCode = 0;
    }
  }else if( rp->lhsalias==0 ){
    /* There is no LHS value symbol. */
    lhsdirect = 1;
  }else if( strcmp(rp->lhsalias,rp->rhsalias[0])==0 ){
    /* The LHS symbol and the left-most RHS symbol are the same, so
    ** direct writing is allowed */
    lhsdirect = 1;
    lhsused = 1;
    used[0] = 1;
    if( rp->lhs->dtnum!=rp->rhs[0]->dtnum ){
      ErrorMsg(lemp->filename,rp->ruleline,
        "%s(%s) and %s(%s) share the same label but have "
        "different datatypes.",
        rp->lhs->name, rp->lhsalias, rp->rhs[0]->name, rp->rhsalias[0]);
      lemp->errorcnt++;
    }
  }else{
    lemon_sprintf(zOvwrt, "/*%s-overwrites-%s*/",
                  rp->lhsalias, rp->rhsalias[0]);
    zSkip = strstr(rp->code, zOvwrt);
    if( zSkip!=0 ){
      /* The code contains a special comment that indicates that it is safe
      ** for the LHS label to overwrite left-most RHS label. */
      lhsdirect = 1;
    }else{
      lhsdirect = 0;
    }
  }
  if( lhsdirect ){
    lemon_sprintf(zLhs, "yymsp[%d].minor.yy%d",1-rp->nrhs,rp->lhs->dtnum);
  }else{
    rc = 1;
    lemon_sprintf(zLhs, "yylhsminor.yy%d",rp->lhs->dtnum);
  }

  append_str(0,0,0,0);

  /* This const cast is wrong but harmless, if we're careful. */
  for(cp=(char *)rp->code; *cp; cp++){
    if( cp==zSkip ){
      append_str(zOvwrt,0,0,0);
      cp += lemonStrlen(zOvwrt)-1;
      dontUseRhs0 = 1;
      continue;
    }
    /* P0-NEW-11: when the walk is inside a string literal, char
    ** literal, or comment, copy the byte verbatim and bypass every
    ** substitution branch below.  All transitions are detected
    ** while in LIME_TC_CODE; we never enter a non-code state from
    ** inside another non-code state (e.g. "//" inside a string
    ** stays a string, "\"" inside a comment stays a comment).
    ** Backslash escapes inside string and char literals consume
    ** the next byte verbatim so that an embedded `\"` or `\'`
    ** does not prematurely close the literal. */
    if( tc_state!=LIME_TC_CODE ){
      switch( tc_state ){
        case LIME_TC_STRING:
          if( *cp=='\\' && cp[1] ){
            append_str(cp, 1, 0, 0);
            cp++;
            append_str(cp, 1, 0, 0);
            continue;
          }
          if( *cp=='"' ){
            tc_state = LIME_TC_CODE;
          }
          append_str(cp, 1, 0, 0);
          continue;
        case LIME_TC_CHAR:
          if( *cp=='\\' && cp[1] ){
            append_str(cp, 1, 0, 0);
            cp++;
            append_str(cp, 1, 0, 0);
            continue;
          }
          if( *cp=='\'' ){
            tc_state = LIME_TC_CODE;
          }
          append_str(cp, 1, 0, 0);
          continue;
        case LIME_TC_LINE_COMMENT:
          if( *cp=='\n' ){
            tc_state = LIME_TC_CODE;
          }
          append_str(cp, 1, 0, 0);
          continue;
        case LIME_TC_BLOCK_COMMENT:
          if( *cp=='*' && cp[1]=='/' ){
            append_str(cp, 1, 0, 0);
            cp++;
            append_str(cp, 1, 0, 0);
            tc_state = LIME_TC_CODE;
            continue;
          }
          append_str(cp, 1, 0, 0);
          continue;
      }
    }
    /* In LIME_TC_CODE: detect entry into a literal or comment
    ** region BEFORE running any substitution logic so that an
    ** identifier-shaped token inside `"..."`, `'...'`, `// ...`,
    ** or `/ * ... * /` is preserved verbatim. */
    if( *cp=='"' ){
      tc_state = LIME_TC_STRING;
      append_str(cp, 1, 0, 0);
      continue;
    }
    if( *cp=='\'' ){
      tc_state = LIME_TC_CHAR;
      append_str(cp, 1, 0, 0);
      continue;
    }
    if( *cp=='/' && cp[1]=='/' ){
      tc_state = LIME_TC_LINE_COMMENT;
      append_str(cp, 1, 0, 0);
      cp++;
      append_str(cp, 1, 0, 0);
      continue;
    }
    if( *cp=='/' && cp[1]=='*' ){
      tc_state = LIME_TC_BLOCK_COMMENT;
      append_str(cp, 1, 0, 0);
      cp++;
      append_str(cp, 1, 0, 0);
      continue;
    }
    /* @$ -- Bison's literal LHS-location syntax.  Lemon historically
    ** has no $$ form (LHS is referenced by alias), but @$ is too
    ** entrenched in bison-derived grammars to ignore.  Expands to
    ** the per-reduce LHS-location local `yyloc_lhs` (computed by
    ** YYLLOC_DEFAULT or its built-in default before the action
    ** body runs; committed to the LHS slot's yyloc field after).
    ** This gives the action body Bison's documented pre-action
    ** ordering: @$ reads the default and may be overwritten by
    ** the action.  See P0-NEW-7 in Lime-Letter-8. */
    if( *cp=='@' && cp[1]=='$' ){
      if( lemp->has_locations ){
        append_str("yyloc_lhs", 0, 0, 0);
      }else{
        ErrorMsg(lemp->filename, rp->ruleline,
          "@$ used but the grammar does not declare %%locations.");
        lemp->errorcnt++;
      }
      cp += 1; /* skip the '$'; outer loop increments past '@' */
      continue;
    }
    if( ISALPHA(*cp) && (cp==rp->code || (!ISALNUM(cp[-1]) && cp[-1]!='_')) ){
      char saved;
      for(xp= &cp[1]; ISALNUM(*xp) || *xp=='_'; xp++);
      saved = *xp;
      *xp = 0;
      if( rp->lhsalias && strcmp(cp,rp->lhsalias)==0 ){
        if( cp!=rp->code && cp[-1]=='@' ){
          /* @<lhsalias> -- LHS location.  P0-NEW-7: expands to the
          ** per-reduce yyloc_lhs local so the action body sees
          ** Bison's documented pre-action default and may overwrite
          ** it.  yyloc_lhs is computed before the action switch by
          ** YYLLOC_DEFAULT (when defined) or by the built-in default
          ** (Rhs[1] for non-empty, lookahead for empty), and is
          ** committed to the LHS slot's yyloc after the action and
          ** stack adjustment. */
          if( lemp->has_locations ){
            append_str("yyloc_lhs", -1, 0, 0);
          }else{
            append_str(zLhs,0,0,0);
          }
        }else{
          append_str(zLhs,0,0,0);
        }
        cp = xp;
        lhsused = 1;
      }else{
        for(i=0; i<rp->nrhs; i++){
          if( rp->rhsalias[i] && strcmp(cp,rp->rhsalias[i])==0 ){
            if( i==0 && dontUseRhs0 ){
              ErrorMsg(lemp->filename,rp->ruleline,
                 "Label %s used after '%s'.",
                 rp->rhsalias[0], zOvwrt);
              lemp->errorcnt++;
            }else if( cp!=rp->code && cp[-1]=='@' ){
              /* @X reference.  When %locations is active, expand to the
              ** stack slot's yyloc field (Bison-compatible @N semantics).
              ** Otherwise fall back to legacy behaviour and emit the
              ** token's enum code.  See P0-NEW-2 in Lime-Letter-4. */
              if( lemp->has_locations ){
                append_str("yymsp[%d].yyloc",-1,i-rp->nrhs+1,0);
              }else{
                append_str("yymsp[%d].major",-1,i-rp->nrhs+1,0);
              }
            }else{
              struct symbol *sp = rp->rhs[i];
              int dtnum;
              if( sp->type==MULTITERMINAL ){
                dtnum = sp->subsym[0]->dtnum;
              }else{
                dtnum = sp->dtnum;
              }
              append_str("yymsp[%d].minor.yy%d",0,i-rp->nrhs+1, dtnum);
            }
            cp = xp;
            used[i] = 1;
            break;
          }
        }
      }
      *xp = saved;
    }
    append_str(cp, 1, 0, 0);
  } /* End loop */

  /* Main code generation completed */
  cp = append_str(0,0,0,0);
  if( cp && cp[0] ) rp->code = Strsafe(cp);
  append_str(0,0,0,0);

  /* Check to make sure the LHS has been used.  We only fire this
  ** check when the rule has an action body, otherwise the alias
  ** can't possibly be "used" -- there's no code to use it from.
  ** Without this guard, generating a parser from a grammar that
  ** lists pure-recogniser rules with explicit aliases (like
  ** PostgreSQL's bare_label_keyword(A) ::= ABORT_P. forms, where
  ** the alias was once needed by an action that has since been
  ** stripped) would produce hundreds of spurious errors.  Note
  ** that rp->code is non-null even for actionless rules (it is
  ** set to a synthetic "\n" earlier in this function); rp->noCode
  ** is the canonical "user wrote no action" flag.
  **
  ** "Unused alias" is a lint warning, not a hard error -- it does
  ** not affect parser correctness (the alias just doesn't connect
  ** to a stack slot read).  Bison treats it the same way.  We
  ** still print the message so a developer sees it, but we do not
  ** count it against errorcnt; this matters for grammars that
  ** were mechanically converted from another generator and may
  ** carry leftover alias labels in their action bodies. */
  if( rp->lhsalias && !lhsused && !rp->noCode ){
    ErrorMsg(lemp->filename,rp->ruleline,
      "Label \"%s\" for \"%s(%s)\" is never used.",
        rp->lhsalias,rp->lhs->name,rp->lhsalias);
    /* warn-only -- see comment above */
  }

  /* Generate destructor code for RHS minor values which are not referenced.
  ** Generate error messages for unused labels and duplicate labels.
  */
  for(i=0; i<rp->nrhs; i++){
    if( rp->rhsalias[i] ){
      if( i>0 ){
        int j;
        if( rp->lhsalias && strcmp(rp->lhsalias,rp->rhsalias[i])==0 ){
          ErrorMsg(lemp->filename,rp->ruleline,
            "%s(%s) has the same label as the LHS but is not the left-most "
            "symbol on the RHS.",
            rp->rhs[i]->name, rp->rhsalias[i]);
          lemp->errorcnt++;
        }
        for(j=0; j<i; j++){
          if( rp->rhsalias[j] && strcmp(rp->rhsalias[j],rp->rhsalias[i])==0 ){
            ErrorMsg(lemp->filename,rp->ruleline,
              "Label %s used for multiple symbols on the RHS of a rule.",
              rp->rhsalias[i]);
            lemp->errorcnt++;
            break;
          }
        }
      }
      if( !used[i] && !rp->noCode ){
        /* See guard rationale on the LHS-alias check above.  This
        ** is also a warn-only diagnostic -- alias being unused is
        ** lint, not a correctness problem. */
        ErrorMsg(lemp->filename,rp->ruleline,
          "Label %s for \"%s(%s)\" is never used.",
          rp->rhsalias[i],rp->rhs[i]->name,rp->rhsalias[i]);
      }
    }else if( i>0 && has_destructor(rp->rhs[i],lemp) ){
      append_str("  yy_destructor(yypParser,%d,&yymsp[%d].minor);\n", 0,
         rp->rhs[i]->index,i-rp->nrhs+1);
    }
  }

  /* If unable to write LHS values directly into the stack, write the
  ** saved LHS value now. */
  if( lhsdirect==0 ){
    append_str("  yymsp[%d].minor.yy%d = ", 0, 1-rp->nrhs, rp->lhs->dtnum);
    append_str(zLhs, 0, 0, 0);
    append_str(";\n", 0, 0, 0);
  }

  /* Suffix code generation complete */
  cp = append_str(0,0,0,0);
  if( cp && cp[0] ){
    rp->codeSuffix = Strsafe(cp);
    rp->noCode = 0;
  }

  return rc;
}

/*
** Generate code which executes when the rule "rp" is reduced.  Write
** the code to "out".  Make sure lineno stays up-to-date.
*/
PRIVATE void emit_code(
  FILE *out,
  struct rule *rp,
  struct lime *lemp,
  int *lineno
){
 const char *cp;

 /* Setup code prior to the #line directive */
 if( rp->codePrefix && rp->codePrefix[0] ){
   fprintf(out, "{%s", rp->codePrefix);
   for(cp=rp->codePrefix; *cp; cp++){ if( *cp=='\n' ) (*lineno)++; }
 }

 /* Generate code to do the reduce action */
 if( rp->code ){
   if( !lemp->nolinenosflag ){
     (*lineno)++;
     tplt_linedir(out,rp->line,lemp->filename);
   }
   fprintf(out,"{%s",rp->code);
   for(cp=rp->code; *cp; cp++){ if( *cp=='\n' ) (*lineno)++; }
   fprintf(out,"}\n"); (*lineno)++;
   if( !lemp->nolinenosflag ){
     (*lineno)++;
     tplt_linedir(out,*lineno,lemp->outname);
   }
 }

 /* Generate breakdown code that occurs after the #line directive */
 if( rp->codeSuffix && rp->codeSuffix[0] ){
   fprintf(out, "%s", rp->codeSuffix);
   for(cp=rp->codeSuffix; *cp; cp++){ if( *cp=='\n' ) (*lineno)++; }
 }

 if( rp->codePrefix ){
   fprintf(out, "}\n"); (*lineno)++;
 }

 return;
}

/*
** Print the definition of the union used for the parser's data stack.
** This union contains fields for every possible data type for tokens
** and nonterminals.  In the process of computing and printing this
** union, also set the ".dtnum" field of every terminal and nonterminal
** symbol.
*/
void print_stack_union(
  FILE *out,                  /* The output stream */
  struct lime *lemp,         /* The main info structure for this parser */
  int *plineno,               /* Pointer to the line number */
  int mhflag                  /* True if generating makeheaders output */
){
  int lineno;               /* The line number of the output */
  char **types;             /* A hash table of datatypes */
  int arraysize;            /* Size of the "types" array */
  int maxdtlength;          /* Maximum length of any ".datatype" field. */
  char *stddt;              /* Standardized name for a datatype */
  int i,j;                  /* Loop counters */
  unsigned hash;            /* For hashing the name of a type */
  const char *name;         /* Name of the parser */

  /* Allocate and initialize types[] and allocate stddt[] */
  arraysize = lemp->nsymbol * 2;
  types = (char**)lime_calloc( arraysize, sizeof(char*) );
  if( types==0 ){
    fprintf(stderr,"Out of memory.\n");
    exit(1);
  }
  for(i=0; i<arraysize; i++) types[i] = 0;
  maxdtlength = 0;
  if( lemp->vartype ){
    maxdtlength = lemonStrlen(lemp->vartype);
  }
  for(i=0; i<lemp->nsymbol; i++){
    int len;
    struct symbol *sp = lemp->symbols[i];
    if( sp->datatype==0 ) continue;
    len = lemonStrlen(sp->datatype);
    if( len>maxdtlength ) maxdtlength = len;
  }
  stddt = (char*)lime_malloc( maxdtlength*2 + 1 );
  if( stddt==0 ){
    fprintf(stderr,"Out of memory.\n");
    exit(1);
  }

  /* Build a hash table of datatypes. The ".dtnum" field of each symbol
  ** is filled in with the hash index plus 1.  A ".dtnum" value of 0 is
  ** used for terminal symbols.  If there is no %default_type defined then
  ** 0 is also used as the .dtnum value for nonterminals which do not specify
  ** a datatype using the %type directive.
  */
  for(i=0; i<lemp->nsymbol; i++){
    struct symbol *sp = lemp->symbols[i];
    char *cp;
    if( sp==lemp->errsym ){
      sp->dtnum = arraysize+1;
      continue;
    }
    if( sp->type!=NONTERMINAL || (sp->datatype==0 && lemp->vartype==0) ){
      sp->dtnum = 0;
      continue;
    }
    cp = sp->datatype;
    if( cp==0 ) cp = lemp->vartype;
    j = 0;
    while( ISSPACE(*cp) ) cp++;
    while( *cp ) stddt[j++] = *cp++;
    while( j>0 && ISSPACE(stddt[j-1]) ) j--;
    stddt[j] = 0;
    if( lemp->tokentype && strcmp(stddt, lemp->tokentype)==0 ){
      sp->dtnum = 0;
      continue;
    }
    hash = 0;
    for(j=0; stddt[j]; j++){
      hash = hash*53 + stddt[j];
    }
    hash = (hash & 0x7fffffff)%arraysize;
    while( types[hash] ){
      if( strcmp(types[hash],stddt)==0 ){
        sp->dtnum = hash + 1;
        break;
      }
      hash++;
      if( hash>=(unsigned)arraysize ) hash = 0;
    }
    if( types[hash]==0 ){
      sp->dtnum = hash + 1;
      types[hash] = (char*)lime_malloc( lemonStrlen(stddt)+1 );
      if( types[hash]==0 ){
        fprintf(stderr,"Out of memory.\n");
        exit(1);
      }
      lemon_strcpy(types[hash],stddt);
    }
  }

  /* Print out the definition of YYTOKENTYPE and YYMINORTYPE */
  name = lemp->name ? lemp->name : "Parse";
  lineno = *plineno;
  if( mhflag ){ fprintf(out,"#if INTERFACE\n"); lineno++; }
  fprintf(out,"#define %sTOKENTYPE %s\n",name,
    lemp->tokentype?lemp->tokentype:"void*");  lineno++;
  if( mhflag ){ fprintf(out,"#endif\n"); lineno++; }
  fprintf(out,"typedef union {\n"); lineno++;
  fprintf(out,"  int yyinit;\n"); lineno++;
  fprintf(out,"  %sTOKENTYPE yy0;\n",name); lineno++;
  for(i=0; i<arraysize; i++){
    if( types[i]==0 ) continue;
    fprintf(out,"  %s yy%d;\n",types[i],i+1); lineno++;
    lime_free(types[i]);
  }
  if( lemp->errsym && lemp->errsym->useCnt ){
    fprintf(out,"  int yy%d;\n",lemp->errsym->dtnum); lineno++;
  }
  lime_free(stddt);
  lime_free(types);
  fprintf(out,"} YYMINORTYPE;\n"); lineno++;
  *plineno = lineno;
}

/*
** Return the name of a C datatype able to represent values between
** lwr and upr, inclusive.  If pnByte!=NULL then also write the sizeof
** for that type (1, 2, or 4) into *pnByte.
*/
static const char *minimum_size_type(int lwr, int upr, int *pnByte){
  const char *zType = "int";
  int nByte = 4;
  if( lwr>=0 ){
    if( upr<=255 ){
      zType = "unsigned char";
      nByte = 1;
    }else if( upr<65535 ){
      zType = "unsigned short int";
      nByte = 2;
    }else{
      zType = "unsigned int";
      nByte = 4;
    }
  }else if( lwr>=-127 && upr<=127 ){
    zType = "signed char";
    nByte = 1;
  }else if( lwr>=-32767 && upr<32767 ){
    zType = "short";
    nByte = 2;
  }
  if( pnByte ) *pnByte = nByte;
  return zType;
}

/*
** Each state contains a set of token transaction and a set of
** nonterminal transactions.  Each of these sets makes an instance
** of the following structure.  An array of these structures is used
** to order the creation of entries in the yy_action[] table.
*/
struct axset {
  struct state *stp;   /* A pointer to a state */
  int isTkn;           /* True to use tokens.  False for non-terminals */
  int nAction;         /* Number of actions */
  int iOrder;          /* Original order of action sets */
};

/*
** Compare to axset structures for sorting purposes
*/
static int axset_compare(const void *a, const void *b){
  struct axset *p1 = (struct axset*)a;
  struct axset *p2 = (struct axset*)b;
  int c;
  c = p2->nAction - p1->nAction;
  if( c==0 ){
    c = p1->iOrder - p2->iOrder;
  }
  assert( c!=0 || p1==p2 );
  return c;
}

/*
** Write text on "out" that describes the rule "rp".
*/
/*
** Find the AST node definition whose name matches (case-insensitively)
** the given non-terminal name, using the ast_prefix if set.
** For example, nonterminal "expr" matches ast_node "Expr" with prefix "Ast".
*/
static struct ast_node_def *find_ast_node(struct lime *lemp, const char *ntName){
  struct ast_node_def *nd;
  for(nd=lemp->ast_nodes; nd; nd=nd->next){
    /* Compare lowercased versions */
    const char *a = ntName;
    const char *b = nd->name;
    while( *a && *b ){
      char ca = ISLOWER(*a) ? *a : (char)(*a + ('a'-'A'));
      char cb = ISLOWER(*b) ? *b : (char)(*b + ('a'-'A'));
      if( ca!=cb ) break;
      a++; b++;
    }
    if( *a==0 && *b==0 ) return nd;
  }
  return 0;
}

/*
** Generate automatic reduction actions for rules when %ast_auto is active.
** Called from main() before ReportTable().
**
** For each rule:
**   - If it already has an explicit action, skip it.
**   - If the LHS non-terminal matches an %ast_node type, generate
**     an AST_NEW() call that maps RHS elements to fields by position.
**   - RHS non-terminals map to pointer fields, terminals to value fields.
**   - If the count doesn't match, issue a warning and skip.
*/
static void GenerateASTActions(struct lime *lemp){
  struct rule *rp;
  const char *ap = lemp->ast_prefix ? lemp->ast_prefix : "";

  if( !lemp->ast_auto || !lemp->ast_nodes ) return;

  for(rp=lemp->rule; rp; rp=rp->next){
    struct ast_node_def *nd;
    struct ast_field *fld;
    int nfields, i;
    char *buf;
    int bufsize, pos;

    /* Skip rules that already have explicit actions */
    if( rp->code!=0 ) continue;

    /* Find matching AST node for the LHS */
    nd = find_ast_node(lemp, rp->lhs->name);
    if( nd==0 ) continue;

    if( nd->is_list ){
      /* For list nodes with exactly 1 RHS that matches element type, generate
      ** a single-element list creation.  For 2 RHS (list + element), generate
      ** an append. */
      if( rp->nrhs==1 ){
        /* list ::= element. --> create new list, append element */
        bufsize = 256 + (int)strlen(ap) + (int)strlen(nd->name);
        buf = (char*)lime_malloc(bufsize);
        if( !buf ) continue;
        lemon_sprintf(buf,
          "\n  A = AST_NEW(arena, %s);\n"
          "  %s_%s_append(arena, A, B);\n",
          nd->name, ap, nd->name);
        rp->code = Strsafe(buf);
        rp->line = rp->ruleline;
        rp->noCode = 0;
        lime_free(buf);
      }else if( rp->nrhs==2 ){
        /* list ::= list element. --> append element to list */
        bufsize = 256 + (int)strlen(ap) + (int)strlen(nd->name);
        buf = (char*)lime_malloc(bufsize);
        if( !buf ) continue;
        lemon_sprintf(buf,
          "\n  A = B;\n"
          "  %s_%s_append(arena, A, C);\n",
          ap, nd->name);
        rp->code = Strsafe(buf);
        rp->line = rp->ruleline;
        rp->noCode = 0;
        lime_free(buf);
      }
      continue;
    }

    /* Count fields in the AST node */
    nfields = 0;
    for(fld=nd->fields; fld; fld=fld->next) nfields++;

    /* Count meaningful RHS elements (those with aliases, or all if none have aliases) */
    /* Generate AST_NEW call: A = AST_NEW(arena, NodeType, B, C, D, ...); */

    /* Build the action string */
    bufsize = 128 + (int)strlen(ap) + (int)strlen(nd->name) + rp->nrhs * 8;
    buf = (char*)lime_malloc(bufsize);
    if( !buf ) continue;

    pos = lemon_sprintf(buf, "\n  A = %s_new_%s(arena", ap, nd->name);

    /* Map RHS to fields. Strategy:
    ** - If the number of RHS symbols equals the number of fields,
    **   map positionally using standard Lime labels (B, C, D, ...).
    ** - Otherwise, skip the rule (too complex for auto-generation).
    */
    if( rp->nrhs == nfields ){
      for(i=0; i<rp->nrhs; i++){
        char label[4];
        label[0] = ','; label[1] = ' ';
        label[2] = (char)('B' + i); label[3] = 0;
        memcpy(buf+pos, label, 3);
        pos += 3;
      }
      memcpy(buf+pos, ");\n", 3);
      pos += 3;
      buf[pos] = 0;
      rp->code = Strsafe(buf);
      rp->line = rp->ruleline;
      rp->noCode = 0;
    }else if( rp->nrhs==0 && nfields==0 ){
      memcpy(buf+pos, ");\n", 3);
      pos += 3;
      buf[pos] = 0;
      rp->code = Strsafe(buf);
      rp->line = rp->ruleline;
      rp->noCode = 0;
    }
    /* else: mismatch, skip - the lint pass will warn about this */

    lime_free(buf);
  }
}

/*
** Generate an ahead-of-time (AOT) compiled action table.
** This produces a *_aot.c file containing a switch-based version
** of yy_find_shift_action() that the C compiler can optimize into
** efficient jump tables, matching JIT performance with zero runtime cost.
*/
void ReportAOTTable(struct lime *lemp){
  FILE *out;
  int i;
  char *z;

  /* Build filename: replace suffix with _aot.c */
  z = file_makename(lemp, "_aot.c");
  out = fopen(z,"wb");
  if( out==0 ){
    fprintf(stderr,"Can't open file \"%s\".\n", z);
    lime_free(z);
    return;
  }
  lime_free(z);

  fprintf(out,
    "/*\n"
    "** AOT-compiled action table for %s.\n"
    "** Generated by Lime with the -j flag.\n"
    "**\n"
    "** This file replaces the table-driven yy_find_shift_action()\n"
    "** with an equivalent switch-based implementation that the C\n"
    "** compiler optimizes into jump tables.\n"
    "**\n"
    "** To use: compile with -DYYAOT and link this file.\n"
    "*/\n\n",
    lemp->filename);

  fprintf(out, "#include <stdint.h>\n\n");

  /* Generate the YYACTIONTYPE typedef matching the parser */
  {
    int mx = lemp->maxAction;
    const char *type;
    if( mx <= 255 ) type = "unsigned char";
    else if( mx <= 65535 ) type = "unsigned short";
    else type = "unsigned int";
    fprintf(out, "typedef %s YYACTIONTYPE_AOT;\n\n", type);
  }

  fprintf(out,
    "YYACTIONTYPE_AOT yy_find_shift_action_aot(\n"
    "  YYACTIONTYPE_AOT stateno,\n"
    "  unsigned short iLookAhead\n"
    "){\n");

  fprintf(out, "  switch(stateno){\n");

  for(i=0; i<lemp->nxstate; i++){
    struct state *stp = lemp->sorted[i];
    struct action *ap;
    int has_actions = 0;

    /* Check if this state has any explicit actions to emit.
    ** Letter 16 (May 2026) -- PG re-ran the verification sweep
    ** I recommended in Reply 15 and found a second AOT codegen
    ** omission: ERROR and ACCEPT action types were silently
    ** dropped from the per-state action loop, so states with
    ** explicit error overrides (state, lookahead) -> error in
    ** the LALR analysis fell through to the default reduce.
    ** PG's gram.lime hit this on ~30 (state, lookahead) pairs.
    ** Counting ERROR / ACCEPT here, and emitting them in the
    ** loop below, makes the AOT path produce identical actions
    ** to the table-driven path on every (s, la) pair.
    **
    ** Crucial filter: skip actions attached to NON-terminal
    ** symbols.  yy_find_shift_action_aot is the token-side
    ** lookup, so the switch may only contain terminal cases.
    ** Mirrors the table-driven emit at lime.c:6467
    ** (`if (ap->sp->index >= lemp->nterminal) continue;`),
    ** which is what keeps ACCEPT out of yy_action[]'s token
    ** half -- ACCEPT in Lemon attaches to the start
    ** non-terminal (lime.c:1398) and is detected on the reduce
    ** side, not the shift side. */
    for(ap=stp->ap; ap; ap=ap->next){
      if( ap->sp->index>=lemp->nterminal ) continue;
      if( ap->type==SHIFT || ap->type==SHIFTREDUCE
          || ap->type==REDUCE || ap->type==ERROR ){
        has_actions = 1;
        break;
      }
    }

    fprintf(out, "    case %d:\n", stp->statenum);
    if( has_actions ){
      fprintf(out, "      switch(iLookAhead){\n");
      for(ap=stp->ap; ap; ap=ap->next){
        /* Same terminal-only filter as the has_actions sweep
        ** above; mirrors lime.c:6467 in the table-driven emit. */
        if( ap->sp->index>=lemp->nterminal ) continue;
        if( ap->type==SHIFT ){
          fprintf(out, "        case %d: return %d; /* %s -> shift state %d */\n",
                  ap->sp->index,
                  ap->x.stp->statenum,
                  ap->sp->name,
                  ap->x.stp->statenum);
        }else if( ap->type==SHIFTREDUCE ){
          fprintf(out, "        case %d: return %d; /* %s -> shift-reduce rule %d */\n",
                  ap->sp->index,
                  ap->x.rp->iRule + lemp->minShiftReduce,
                  ap->sp->name,
                  ap->x.rp->iRule);
        }else if( ap->type==REDUCE ){
          fprintf(out, "        case %d: return %d; /* %s -> reduce rule %d */\n",
                  ap->sp->index,
                  ap->x.rp->iRule + lemp->minReduce,
                  ap->sp->name,
                  ap->x.rp->iRule);
        }else if( ap->type==ERROR ){
          /* Explicit (state, lookahead) -> error override from
          ** the LALR analysis.  Mirrors lime.c:4786 in the
          ** table-driven emit (case ERROR: act = errAction).
          ** Without this case the AOT loop falls through to
          ** the state default, which on default-reduce states
          ** silently reduces instead of erroring -- the bug
          ** Letter 16 reported. */
          fprintf(out, "        case %d: return %d; /* %s -> error (explicit override) */\n",
                  ap->sp->index,
                  lemp->errAction,
                  ap->sp->name);
        }
        /* Note: ACCEPT, SSCONFLICT, SRCONFLICT, RRCONFLICT,
        ** SH_RESOLVED, RD_RESOLVED, NOT_USED do not reach this
        ** point.  ACCEPT is filtered above (nonterminal-attached);
        ** the conflict / resolved / not-used types are collapsed
        ** by CompressTables before ReportAOTTable runs.  We don't
        ** sanity-assert because the table-driven emit at
        ** lime.c:4786 doesn't either, and adding an assert in
        ** lime would diverge from Lemon's "trust the table
        ** compiler" stance. */
      }
      /* Per-state default.  Mirrors the table-driven yy_default[]
      ** emit at lime.c:6611: when iDfltReduce<0 the state has no
      ** default reduce and a lookahead miss is a syntax error;
      ** otherwise the lookahead miss reduces by iDfltReduce.
      ** PG's Letter 15 (May 2026) reported this site emitting a
      ** uniform YY_NO_ACTION sentinel, which the parser core
      ** then rejected via assert(yyact == YY_ERROR_ACTION) on
      ** the 65 % of states with no default reduce, and which
      ** caused silent reduce-skipping on the 35 % that did. */
      if( stp->iDfltReduce<0 ){
        fprintf(out, "        default: return %d; /* error action */\n",
                lemp->errAction);
      }else{
        fprintf(out, "        default: return %d; /* default reduce rule %d */\n",
                stp->iDfltReduce + lemp->minReduce,
                stp->iDfltReduce);
      }
      fprintf(out, "      }\n");
    }else{
      /* Same per-state-default rule as above, applied to the
      ** "no shift/shiftreduce actions" branch (state has only
      ** reduces or is the empty start state).  iDfltReduce
      ** drives the choice. */
      if( stp->iDfltReduce<0 ){
        fprintf(out, "      return %d; /* error action */\n", lemp->errAction);
      }else{
        fprintf(out, "      return %d; /* default reduce rule %d */\n",
                stp->iDfltReduce + lemp->minReduce,
                stp->iDfltReduce);
      }
    }
  }

  /* Outer state-not-found.  yy_find_shift_action's caller has
  ** already guarded `stateno > YY_MAX_SHIFT` (which short-
  ** circuits to passthrough) so reaching this branch means a
  ** state code that was never enumerated -- i.e., corrupt input.
  ** YY_ERROR_ACTION is the right sentinel; YY_NO_ACTION is a
  ** marker for empty action-table slots and the parser's outer
  ** loop must never see it for a state it considers valid. */
  fprintf(out, "    default: return %d;\n", lemp->errAction);
  fprintf(out, "  }\n");
  fprintf(out, "}\n");
  fclose(out);
}

static void writeRuleText(FILE *out, struct rule *rp){
  int j;
  fprintf(out,"%s ::=", rp->lhs->name);
  for(j=0; j<rp->nrhs; j++){
    struct symbol *sp = rp->rhs[j];
    if( sp->type!=MULTITERMINAL ){
      fprintf(out," %s", sp->name);
    }else{
      int k;
      fprintf(out," %s", sp->subsym[0]->name);
      for(k=1; k<sp->nsubsym; k++){
        fprintf(out,"|%s",sp->subsym[k]->name);
      }
    }
  }
}

/*
** Return true if the string is not NULL and not empty.
*/
static int notnull(const char *z){
  return z && z[0];
}


/*
** Emit the %symbol_prefix #define block at the top of a generated
** source or header file.  When the user wrote %symbol_prefix CB_,
** every internal YY_* macro and yy* type/function name gets aliased
** to CB_<name> via the preprocessor, so two grammars combined into
** one translation unit (or examined via nm / debug info) do not
** collide on the internal namespace.
**
** When no prefix is set, this emits nothing.  The list of names
** below is the closed set of identifiers the limpar.c template and
** ReportTable() itself emit; a future template change that adds new
** internal names should also extend this list.
*/
static void emit_symbol_prefix_block(FILE *out, struct lime *lemp, int *lineno) {
  if (lemp->symbolprefix == NULL || lemp->symbolprefix[0] == '\0') return;

  /* Strip any surrounding {} that the directive parser leaves on the
  ** value (Lime's brace-arg parsing wraps multi-token values). */
  char *p = lemp->symbolprefix;
  while (*p == '{' || ISSPACE(*p)) p++;
  size_t plen = strlen(p);
  while (plen > 0 && (p[plen-1] == '}' || ISSPACE(p[plen-1]))) plen--;
  if (plen == 0) return;

  /* Stash the cleaned prefix back on lemp so subsequent emissions
  ** (e.g. ReportHeader) can use it without re-cleaning. */
  static char cleaned[64];
  if (plen >= sizeof(cleaned)) plen = sizeof(cleaned) - 1;
  memcpy(cleaned, p, plen);
  cleaned[plen] = '\0';
  lemp->symbolprefix = cleaned;

  static const char *yy_names[] = {
    /* Action and rule tables emitted by ReportTable */
    "yy_action", "yy_lookahead", "yy_shift_ofst", "yy_reduce_ofst",
    "yy_default", "yyTokenName", "yyRuleName",
    "yyRuleInfoLhs", "yyRuleInfoNRhs",
    /* Trace globals */
    "yyTraceFILE", "yyTracePrompt",
    /* Internal types */
    "yyParser", "yyStackEntry",
    /* Internal helpers (file-static today, but their names still appear
    ** in debug info / object dumps).  yyGrowStack is intentionally
    ** omitted -- limpar.c may #define it to a no-op macro under
    ** #if !YYGROWABLESTACK, and the prefix would then collide with
    ** that conditional redefinition. */
    "yy_destructor", "yy_pop_parser_stack",
    "yy_find_shift_action", "yy_find_reduce_action", "yyStackOverflow",
    "yyTraceShift", "yy_shift", "yy_reduce", "yy_accept",
    "yy_parse_failed", "yy_syntax_error",
    NULL,
  };
  /*
  ** YY_* macros INTENTIONALLY excluded from the prefix block:
  ** they are compile-time integer / string constants that never
  ** appear in the object file's symbol table, so prefixing them
  ** yields no link-time benefit.  Doing so would also generate
  ** spurious -Wmacro-redefined warnings because lime's emit
  ** subsequently re-#defines them with their actual values, and
  ** would break #if expressions that test their values.
  **
  ** The collision risk addressed by %symbol_prefix is the
  ** SYMBOL-table side: yy_action, yyParser, yy_destructor, etc.
  ** Those are file-static names that nm and the linker observe.
  ** They get prefixed below.
  */
  static const char *yy_macros[] = {
    NULL,
  };

  fprintf(out,
    "/* symbol_prefix=%s -- alias every internal YY_ macro and yy\n"
    "** symbol so this grammar's internal namespace does not collide\n"
    "** with another Lime-generated parser linked into the same\n"
    "** binary.  See lime.c::emit_symbol_prefix_block. */\n",
    cleaned);
  *lineno += 4;
  for (int i = 0; yy_names[i] != NULL; i++) {
    fprintf(out, "#define %s %s%s\n", yy_names[i], cleaned, yy_names[i]);
    (*lineno)++;
  }
  for (int i = 0; yy_macros[i] != NULL; i++) {
    fprintf(out, "#define %s %s%s\n", yy_macros[i], cleaned, yy_macros[i]);
    (*lineno)++;
  }
  fprintf(out, "\n");
  (*lineno)++;
}

/* Generate C source code for the parser */
void ReportTable(
  struct lime *lemp,
  int mhflag,     /* Output in makeheaders format if true */
  int sqlFlag     /* Generate the *.sql file too */
){
  FILE *out, *in;
  int  lineno;
  struct state *stp;
  struct action *ap;
  struct rule *rp;
  struct acttab *pActtab;
  int i, j, n, sz, mn, mx;
  int nLookAhead;
  int szActionType;     /* sizeof(YYACTIONTYPE) */
  int szCodeType;       /* sizeof(YYCODETYPE)   */
  const char *name;
  int mnTknOfst, mxTknOfst;
  int mnNtOfst, mxNtOfst;
  struct axset *ax;
  char *prefix;

  lemp->minShiftReduce = lemp->nstate;
  lemp->errAction = lemp->minShiftReduce + lemp->nrule;
  lemp->accAction = lemp->errAction + 1;
  lemp->noAction = lemp->accAction + 1;
  lemp->minReduce = lemp->noAction + 1;
  lemp->maxAction = lemp->minReduce + lemp->nrule;

  in = tplt_open(lemp);
  if( in==0 ) return;
  if( sqlFlag ){
    FILE *sql = file_open(lemp, ".sql", "wb");
    if( sql==0 ){
      fclose(in);
      return;
    }
    fprintf(sql,
       "BEGIN;\n"
       "CREATE TABLE symbol(\n"
       "  id INTEGER PRIMARY KEY,\n"
       "  name TEXT NOT NULL,\n"
       "  isTerminal BOOLEAN NOT NULL,\n"
       "  fallback INTEGER REFERENCES symbol"
               " DEFERRABLE INITIALLY DEFERRED\n"
       ");\n"
    );
    for(i=0; i<lemp->nsymbol; i++){
      fprintf(sql,
         "INSERT INTO symbol(id,name,isTerminal,fallback)"
         "VALUES(%d,'%s',%s",
         i, lemp->symbols[i]->name,
         i<lemp->nterminal ? "TRUE" : "FALSE"
      );
      if( lemp->symbols[i]->fallback ){
        fprintf(sql, ",%d);\n", lemp->symbols[i]->fallback->index);
      }else{
        fprintf(sql, ",NULL);\n");
      }
    }
    fprintf(sql,
      "CREATE TABLE rule(\n"
      "  ruleid INTEGER PRIMARY KEY,\n"
      "  lhs INTEGER REFERENCES symbol(id),\n"
      "  txt TEXT\n"
      ");\n"
      "CREATE TABLE rulerhs(\n"
      "  ruleid INTEGER REFERENCES rule(ruleid),\n"
      "  pos INTEGER,\n"
      "  sym INTEGER REFERENCES symbol(id)\n"
      ");\n"
    );
    for(i=0, rp=lemp->rule; rp; rp=rp->next, i++){
      assert( i==rp->iRule );
      fprintf(sql,
        "INSERT INTO rule(ruleid,lhs,txt)VALUES(%d,%d,'",
        rp->iRule, rp->lhs->index
      );
      writeRuleText(sql, rp);
      fprintf(sql,"');\n");
      for(j=0; j<rp->nrhs; j++){
        struct symbol *sp = rp->rhs[j];
        if( sp->type!=MULTITERMINAL ){
          fprintf(sql,
            "INSERT INTO rulerhs(ruleid,pos,sym)VALUES(%d,%d,%d);\n",
            i,j,sp->index
          );
        }else{
          int k;
          for(k=0; k<sp->nsubsym; k++){
            fprintf(sql,
              "INSERT INTO rulerhs(ruleid,pos,sym)VALUES(%d,%d,%d);\n",
              i,j,sp->subsym[k]->index
            );
          }
        }
      }
    }
    fprintf(sql, "COMMIT;\n");
    fclose(sql);
  }
  out = file_open(lemp,".c","wb");
  if( out==0 ){
    fclose(in);
    return;
  }
  lineno = 1;

  fprintf(out, 
     "/* This file is automatically generated by Lemon from input grammar\n"
     "** source file \"%s\"", lemp->filename);  lineno++;
  if( nDefineUsed==0 ){
    fprintf(out, ".\n*/\n"); lineno += 2;
  }else{
    fprintf(out, " with these options:\n**\n"); lineno += 2;
    for(i=0; i<nDefine; i++){
      if( !bDefineUsed[i] ) continue;
      fprintf(out, "**   -D%s\n", azDefine[i]); lineno++;
    }
    fprintf(out, "*/\n"); lineno++;
  }

  /* %symbol_prefix block: alias every internal YY_ macro and yy*
  ** name so this grammar's namespace doesn't collide with another
  ** Lime parser linked into the same image.  No-op when
  ** %symbol_prefix is unset. */
  emit_symbol_prefix_block(out, lemp, &lineno);

  /* The first %include directive begins with a C-language comment,
  ** then skip over the header comment of the template file
  */
  if( lemp->include==0 ) lemp->include = "";
  for(i=0; ISSPACE(lemp->include[i]); i++){
    if( lemp->include[i]=='\n' ){
      lemp->include += i+1;
      i = -1;
    }
  }
  if( lemp->include[0]=='/' ){
    tplt_skip_header(in);
  }else{
    tplt_xfer(lemp->name,in,out,&lineno);
  }

  /* Generate the include code, if any */
  tplt_print(out,lemp,lemp->include,&lineno);
  if( mhflag ){
    char *incName = file_makename(lemp, ".h");
    fprintf(out,"#include \"%s\"\n", incName); lineno++;
    lime_free(incName);
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate #defines for all tokens */
  if( lemp->tokenprefix ) prefix = lemp->tokenprefix;
  else                    prefix = "";
  if( mhflag ){
    fprintf(out,"#if INTERFACE\n"); lineno++;
  }else{
    fprintf(out,"#ifndef %s%s\n", prefix, lemp->symbols[1]->name); lineno++;
  }
  for(i=1; i<lemp->nterminal; i++){
    fprintf(out,"#define %s%-30s %2d\n",prefix,lemp->symbols[i]->name,i);
    lineno++;
  }
  fprintf(out,"#endif\n"); lineno++;
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate the defines */
  fprintf(out,"#define YYCODETYPE %s\n",
    minimum_size_type(0, lemp->nsymbol, &szCodeType)); lineno++;
  fprintf(out,"#define YYNOCODE %d\n",lemp->nsymbol);  lineno++;
  fprintf(out,"#define YYACTIONTYPE %s\n",
    minimum_size_type(0,lemp->maxAction,&szActionType)); lineno++;
  if( lemp->wildcard ){
    fprintf(out,"#define YYWILDCARD %d\n",
       lemp->wildcard->index); lineno++;
  }
  /* %first_token N -- offset added to externally-visible token codes.
  ** When zero (the default), the parser is binary-identical to a
  ** pre-%first_token build; the runtime template's offset arithmetic
  ** compiles down to nothing. */
  fprintf(out,"#define YYFIRSTTOKEN %d\n", lemp->first_token); lineno++;
  /* P0-NEW-6: longest RHS in the grammar.  Used in yy_reduce to bound
  ** the on-stack YYLOCATIONTYPE array passed to a user-defined
  ** YYLLOC_DEFAULT macro per Bison's signature.  Computed from
  ** rp->nrhs across all rules; floor of 1 so the array declaration
  ** is always well-formed even for the degenerate empty-grammar case. */
  {
    struct rule *rp;
    int yymaxrhs = 0;
    for(rp=lemp->rule; rp; rp=rp->next){
      if( rp->nrhs > yymaxrhs ) yymaxrhs = rp->nrhs;
    }
    if( yymaxrhs < 1 ) yymaxrhs = 1;
    fprintf(out,"#define YYNRHS_MAX %d\n", yymaxrhs); lineno++;
  }
  print_stack_union(out,lemp,&lineno,mhflag);
  fprintf(out, "#ifndef YYSTACKDEPTH\n"); lineno++;
  if( lemp->stacksize ){
    fprintf(out,"#define YYSTACKDEPTH %s\n",lemp->stacksize);  lineno++;
  }else{
    fprintf(out,"#define YYSTACKDEPTH 100\n");  lineno++;
  }
  fprintf(out, "#endif\n"); lineno++;
  if( mhflag ){
    fprintf(out,"#if INTERFACE\n"); lineno++;
  }
  name = lemp->name ? lemp->name : "Parse";
  if( lemp->arg && lemp->arg[0] ){
    i = lemonStrlen(lemp->arg);
    while( i>=1 && ISSPACE(lemp->arg[i-1]) ) i--;
    while( i>=1 && (ISALNUM(lemp->arg[i-1]) || lemp->arg[i-1]=='_') ) i--;
    fprintf(out,"#define %sARG_SDECL %s;\n",name,lemp->arg);  lineno++;
    fprintf(out,"#define %sARG_PDECL ,%s\n",name,lemp->arg);  lineno++;
    fprintf(out,"#define %sARG_PARAM ,%s\n",name,&lemp->arg[i]);  lineno++;
    fprintf(out, "#define %sARG_FETCH %s=yypParser->%s; (void)%s;\n", name, lemp->arg,
            &lemp->arg[i], &lemp->arg[i]);
    lineno++;
    fprintf(out, "#define %sARG_STORE yypParser->%s=%s;\n", name, &lemp->arg[i], &lemp->arg[i]);
    lineno++;
  }else{
    fprintf(out,"#define %sARG_SDECL\n",name); lineno++;
    fprintf(out,"#define %sARG_PDECL\n",name); lineno++;
    fprintf(out,"#define %sARG_PARAM\n",name); lineno++;
    fprintf(out,"#define %sARG_FETCH\n",name); lineno++;
    fprintf(out,"#define %sARG_STORE\n",name); lineno++;
  }
  fprintf(out, "#undef YYREALLOC\n"); lineno++;
  if( lemp->reallocFunc ){
    fprintf(out,"#define YYREALLOC %s\n", lemp->reallocFunc); lineno++;
  }else{
    fprintf(out,"#define YYREALLOC realloc\n"); lineno++;
  }
  fprintf(out, "#undef YYFREE\n"); lineno++;
  if( lemp->freeFunc ){
    fprintf(out,"#define YYFREE %s\n", lemp->freeFunc); lineno++;
  }else{
    fprintf(out,"#define YYFREE free\n"); lineno++;
  }
  fprintf(out, "#undef YYDYNSTACK\n"); lineno++;
  if( lemp->reallocFunc && lemp->freeFunc ){
    fprintf(out,"#define YYDYNSTACK 1\n"); lineno++;
  }else{
    fprintf(out,"#define YYDYNSTACK 0\n"); lineno++;
  }
  fprintf(out, "#undef YYSIZELIMIT\n"); lineno++;
  if( notnull(lemp->ctx) ){
    i = lemonStrlen(lemp->ctx);
    while( i>=1 && ISSPACE(lemp->ctx[i-1]) ) i--;
    while( i>=1 && (ISALNUM(lemp->ctx[i-1]) || lemp->ctx[i-1]=='_') ) i--;
    if( notnull(lemp->stackSizeLimit) ){
      fprintf(out,"#define YYSIZELIMIT %s\n", lemp->stackSizeLimit); lineno++;
    }
    fprintf(out,"#define %sCTX(P) ((P)->%s)\n",name,&lemp->ctx[i]); lineno++;
    fprintf(out,"#define %sCTX_SDECL %s;\n",name,lemp->ctx);  lineno++;
    fprintf(out,"#define %sCTX_PDECL ,%s\n",name,lemp->ctx);  lineno++;
    fprintf(out,"#define %sCTX_PARAM ,%s\n",name,&lemp->ctx[i]);  lineno++;
    fprintf(out,"#define %sCTX_FETCH %s=yypParser->%s;\n",
                 name,lemp->ctx,&lemp->ctx[i]);  lineno++;
    fprintf(out,"#define %sCTX_STORE yypParser->%s=%s;\n",
                 name,&lemp->ctx[i],&lemp->ctx[i]);  lineno++;
  }else{
    fprintf(out,"#define %sCTX(P) 0\n",name); lineno++;
    fprintf(out,"#define %sCTX_SDECL\n",name); lineno++;
    fprintf(out,"#define %sCTX_PDECL\n",name); lineno++;
    fprintf(out,"#define %sCTX_PARAM\n",name); lineno++;
    fprintf(out,"#define %sCTX_FETCH\n",name); lineno++;
    fprintf(out,"#define %sCTX_STORE\n",name); lineno++;
  }
  if( mhflag ){
    fprintf(out,"#endif\n"); lineno++;
  }
  fprintf(out, "#undef YYERRORSYMBOL\n"); lineno++;
  fprintf(out, "#undef YYERRSYMDT\n"); lineno++;
  if( lemp->errsym && lemp->errsym->useCnt ){
    fprintf(out,"#define YYERRORSYMBOL %d\n",lemp->errsym->index); lineno++;
    fprintf(out,"#define YYERRSYMDT yy%d\n",lemp->errsym->dtnum); lineno++;
  }
  fprintf(out,"#undef YYFALLBACK\n"); lineno++;
  if( lemp->has_fallback ){
    fprintf(out,"#define YYFALLBACK 1\n");  lineno++;
  }
  if( lemp->has_locations ){
    if( lemp->location_type ){
      /* Caller supplied %location_type {Type}; emit their type and
      ** skip the lime_location.h include -- the user provides the
      ** type definition themselves.  See P0-NEW-2 in Lime-Letter-4. */
      fprintf(out,"#define YYLOCATIONTYPE %s\n", lemp->location_type);
      lineno++;
    }else{
      fprintf(out,"#include \"lime_location.h\"\n"); lineno++;
      fprintf(out,"#define YYLOCATIONTYPE LimeLocation\n"); lineno++;
    }
  }
  if( lemp->n_error_sync_tokens > 0 ){
    fprintf(out,"#define YYERRORSYNC 1\n"); lineno++;
    fprintf(out,"static const unsigned char yy_is_sync_token[] = {\n"); lineno++;
    for(i=0; i<lemp->nsymbol; i++){
      int is_sync = 0;
      for(j=0; j<lemp->n_error_sync_tokens; j++){
        if( strcmp(lemp->symbols[i]->name,
                   lemp->error_sync_tokens[j])==0 ){
          is_sync = 1;
          break;
        }
      }
      fprintf(out,"  %d, /* %s */\n", is_sync, lemp->symbols[i]->name);
      lineno++;
    }
    fprintf(out,"};\n"); lineno++;
  }

  /* Generate AST constructor implementations if %ast_node directives were used */
  if( lemp->ast_nodes ){
    const char *ap = lemp->ast_prefix ? lemp->ast_prefix : "";
    struct ast_node_def *nd;

    fprintf(out,"\n/* AST constructor implementations (generated by Lime) */\n");
    lineno++;
    for(nd=lemp->ast_nodes; nd; nd=nd->next){
      if( nd->is_list ){
        /* List constructor: allocate header */
        fprintf(out,"%s%s *%s_new_%s(LimeArena *a){\n",
                ap, nd->name, ap, nd->name); lineno++;
        fprintf(out,"  %s%s *n = (%s%s*)lime_arena_alloc(a, sizeof(%s%s));\n",
                ap, nd->name, ap, nd->name, ap, nd->name); lineno++;
        fprintf(out,"  if(!n) return 0;\n"); lineno++;
        fprintf(out,"  n->tag = %s%s_TAG;\n", ap, nd->name); lineno++;
        fprintf(out,"  n->items = 0;\n"); lineno++;
        fprintf(out,"  n->count = 0;\n"); lineno++;
        fprintf(out,"  n->capacity = 0;\n"); lineno++;
        fprintf(out,"  return n;\n"); lineno++;
        fprintf(out,"}\n"); lineno++;

        /* List append function */
        fprintf(out,"void %s_%s_append(LimeArena *a, %s%s *list, %s%s *item){\n",
                ap, nd->name, ap, nd->name, ap, nd->element_type); lineno++;
        fprintf(out,"  if(list->count >= list->capacity){\n"); lineno++;
        fprintf(out,"    int newcap = list->capacity ? list->capacity*2 : 4;\n"); lineno++;
        fprintf(out,"    %s%s **newitems = (%s%s**)lime_arena_alloc(a, "
                "newcap*sizeof(%s%s*));\n",
                ap, nd->element_type, ap, nd->element_type,
                ap, nd->element_type); lineno++;
        fprintf(out,"    if(!newitems) return;\n"); lineno++;
        fprintf(out,"    if(list->items){\n"); lineno++;
        fprintf(out,"      int i; for(i=0;i<list->count;i++) newitems[i] = list->items[i];\n"); lineno++;
        fprintf(out,"    }\n"); lineno++;
        fprintf(out,"    list->items = newitems;\n"); lineno++;
        fprintf(out,"    list->capacity = newcap;\n"); lineno++;
        fprintf(out,"  }\n"); lineno++;
        fprintf(out,"  list->items[list->count++] = item;\n"); lineno++;
        fprintf(out,"}\n\n"); lineno += 2;
      }else{
        /* Regular node constructor */
        struct ast_field *fld;
        fprintf(out,"%s%s *%s_new_%s(LimeArena *a",
                ap, nd->name, ap, nd->name);
        for(fld=nd->fields; fld; fld=fld->next){
          fprintf(out,", %s %s_", fld->type, fld->name);
        }
        fprintf(out,"){\n"); lineno++;
        fprintf(out,"  %s%s *n = (%s%s*)lime_arena_alloc(a, sizeof(%s%s));\n",
                ap, nd->name, ap, nd->name, ap, nd->name); lineno++;
        fprintf(out,"  if(!n) return 0;\n"); lineno++;
        fprintf(out,"  n->tag = %s%s_TAG;\n", ap, nd->name); lineno++;
        for(fld=nd->fields; fld; fld=fld->next){
          fprintf(out,"  n->%s = %s_;\n", fld->name, fld->name); lineno++;
        }
        fprintf(out,"  return n;\n"); lineno++;
        fprintf(out,"}\n\n"); lineno += 2;
      }
    }
  }

  /* Compute the action table, but do not output it yet.  The action
  ** table must be computed before generating the YYNSTATE macro because
  ** we need to know how many states can be eliminated.
  */
  ax = (struct axset *) lime_calloc(lemp->nxstate*2, sizeof(ax[0]));
  if( ax==0 ){
    fprintf(stderr,"malloc failed\n");
    exit(1);
  }
  for(i=0; i<lemp->nxstate; i++){
    stp = lemp->sorted[i];
    ax[i*2].stp = stp;
    ax[i*2].isTkn = 1;
    ax[i*2].nAction = stp->nTknAct;
    ax[i*2+1].stp = stp;
    ax[i*2+1].isTkn = 0;
    ax[i*2+1].nAction = stp->nNtAct;
  }
  mxTknOfst = mnTknOfst = 0;
  mxNtOfst = mnNtOfst = 0;
  /* In an effort to minimize the action table size, use the heuristic
  ** of placing the largest action sets first */
  for(i=0; i<lemp->nxstate*2; i++) ax[i].iOrder = i;
  qsort(ax, lemp->nxstate*2, sizeof(ax[0]), axset_compare);
  pActtab = acttab_alloc(lemp->nsymbol, lemp->nterminal);
  for(i=0; i<lemp->nxstate*2 && ax[i].nAction>0; i++){
    stp = ax[i].stp;
    if( ax[i].isTkn ){
      for(ap=stp->ap; ap; ap=ap->next){
        int action;
        if( ap->sp->index>=lemp->nterminal ) continue;
        action = compute_action(lemp, ap);
        if( action<0 ) continue;
        acttab_action(pActtab, ap->sp->index, action);
      }
      stp->iTknOfst = acttab_insert(pActtab, 1);
      if( stp->iTknOfst<mnTknOfst ) mnTknOfst = stp->iTknOfst;
      if( stp->iTknOfst>mxTknOfst ) mxTknOfst = stp->iTknOfst;
    }else{
      for(ap=stp->ap; ap; ap=ap->next){
        int action;
        if( ap->sp->index<lemp->nterminal ) continue;
        if( ap->sp->index==lemp->nsymbol ) continue;
        action = compute_action(lemp, ap);
        if( action<0 ) continue;
        acttab_action(pActtab, ap->sp->index, action);
      }
      stp->iNtOfst = acttab_insert(pActtab, 0);
      if( stp->iNtOfst<mnNtOfst ) mnNtOfst = stp->iNtOfst;
      if( stp->iNtOfst>mxNtOfst ) mxNtOfst = stp->iNtOfst;
    }
#if 0  /* Uncomment for a trace of how the yy_action[] table fills out */
    { int jj, nn;
      for(jj=nn=0; jj<pActtab->nAction; jj++){
        if( pActtab->aAction[jj].action<0 ) nn++;
      }
      printf("%4d: State %3d %s n: %2d size: %5d freespace: %d\n",
             i, stp->statenum, ax[i].isTkn ? "Token" : "Var  ",
             ax[i].nAction, pActtab->nAction, nn);
    }
#endif
  }
  lime_free(ax);

  /* Mark rules that are actually used for reduce actions after all
  ** optimizations have been applied
  */
  for(rp=lemp->rule; rp; rp=rp->next) rp->doesReduce = LEMON_FALSE;
  for(i=0; i<lemp->nxstate; i++){
    for(ap=lemp->sorted[i]->ap; ap; ap=ap->next){
      if( ap->type==REDUCE || ap->type==SHIFTREDUCE ){
        ap->x.rp->doesReduce = 1;
      }
    }
  }

  /* Finish rendering the constants now that the action table has
  ** been computed */
  fprintf(out,"#define YYNSTATE             %d\n",lemp->nxstate);  lineno++;
  fprintf(out,"#define YYNRULE              %d\n",lemp->nrule);  lineno++;
  fprintf(out,"#define YYNRULE_WITH_ACTION  %d\n",lemp->nruleWithAction);
         lineno++;
  fprintf(out,"#define YYNTOKEN             %d\n",lemp->nterminal); lineno++;
  fprintf(out,"#define YY_MAX_SHIFT         %d\n",lemp->nxstate-1); lineno++;
  i = lemp->minShiftReduce;
  fprintf(out,"#define YY_MIN_SHIFTREDUCE   %d\n",i); lineno++;
  i += lemp->nrule;
  fprintf(out,"#define YY_MAX_SHIFTREDUCE   %d\n", i-1); lineno++;
  fprintf(out,"#define YY_ERROR_ACTION      %d\n", lemp->errAction); lineno++;
  fprintf(out,"#define YY_ACCEPT_ACTION     %d\n", lemp->accAction); lineno++;
  fprintf(out,"#define YY_NO_ACTION         %d\n", lemp->noAction); lineno++;
  fprintf(out,"#define YY_MIN_REDUCE        %d\n", lemp->minReduce); lineno++;
  i = lemp->minReduce + lemp->nrule;
  fprintf(out,"#define YY_MAX_REDUCE        %d\n", i-1); lineno++;

  /* Minimum and maximum symbol values that have a destructor.
  ** When no destructors are defined, set mn > mx so comparisons like
  ** "yytos->major >= YY_MIN_DSTRCTR" are always false, avoiding
  ** "comparison is always true" warnings with unsigned YYCODETYPE. */
  mn = mx = 0;
  { int have_dstrctr = 0;
    for(i=0; i<lemp->nsymbol; i++){
      struct symbol *sp = lemp->symbols[i];

      if( sp && sp->type!=TERMINAL && sp->destructor ){
        if( !have_dstrctr || sp->index<mn ) mn = sp->index;
        if( sp->index>mx ) mx = sp->index;
        have_dstrctr = 1;
      }
    }
    if( lemp->tokendest ){ mn = 0; have_dstrctr = 1; }
    if( lemp->vardest ){ mx = lemp->nsymbol-1; have_dstrctr = 1; }
    if( !have_dstrctr ){
      mn = lemp->nsymbol;  /* Empty range: min > max */
      mx = 0;
    }
  }
  fprintf(out,"#define YY_MIN_DSTRCTR       %d\n", mn);  lineno++;
  fprintf(out,"#define YY_MAX_DSTRCTR       %d\n", mx);  lineno++;    

  tplt_xfer(lemp->name,in,out,&lineno);

  /* Now output the action table and its associates:
  **
  **  yy_action[]        A single table containing all actions.
  **  yy_lookahead[]     A table containing the lookahead for each entry in
  **                     yy_action.  Used to detect hash collisions.
  **  yy_shift_ofst[]    For each state, the offset into yy_action for
  **                     shifting terminals.
  **  yy_reduce_ofst[]   For each state, the offset into yy_action for
  **                     shifting non-terminals after a reduce.
  **  yy_default[]       Default action for each state.
  */

  /* Output the yy_action table */
  lemp->nactiontab = n = acttab_action_size(pActtab);
  lemp->tablesize += n*szActionType;
  fprintf(out,"#define YY_ACTTAB_COUNT (%d)\n", n); lineno++;
  fprintf(out,"static const YYACTIONTYPE yy_action[] = {\n"); lineno++;
  /* Save a copy of the action table for snapshot generation */
  lemp->aAction = (int*)lime_calloc(n, sizeof(int));
  for(i=j=0; i<n; i++){
    int action = acttab_yyaction(pActtab, i);
    if( action<0 ) action = lemp->noAction;
    if( lemp->aAction ) lemp->aAction[i] = action;
    if( j==0 ) fprintf(out," /* %5d */ ", i);
    fprintf(out, " %4d,", action);
    if( j==9 || i==n-1 ){
      fprintf(out, "\n"); lineno++;
      j = 0;
    }else{
      j++;
    }
  }
  fprintf(out, "};\n"); lineno++;

  /* Output the yy_lookahead table */
  lemp->nlookaheadtab = n = acttab_lookahead_size(pActtab);
  lemp->tablesize += n*szCodeType;
  fprintf(out,"static const YYCODETYPE yy_lookahead[] = {\n"); lineno++;
  /* The full lookahead array includes padding up to nterminal+nactiontab */
  nLookAhead = lemp->nterminal + lemp->nactiontab;
  if( nLookAhead < n ) nLookAhead = n;
  lemp->nLookahead = nLookAhead;
  lemp->aLookahead = (int*)lime_calloc(nLookAhead, sizeof(int));
  for(i=j=0; i<n; i++){
    int la = acttab_yylookahead(pActtab, i);
    if( la<0 ) la = lemp->nsymbol;
    if( lemp->aLookahead ) lemp->aLookahead[i] = la;
    if( j==0 ) fprintf(out," /* %5d */ ", i);
    fprintf(out, " %4d,", la);
    if( j==9 ){
      fprintf(out, "\n"); lineno++;
      j = 0;
    }else{
      j++;
    }
  }
  /* Add extra entries to the end of the yy_lookahead[] table so that
  ** yy_shift_ofst[]+iToken will always be a valid index into the array,
  ** even for the largest possible value of yy_shift_ofst[] and iToken. */
  while( i<nLookAhead ){
    if( lemp->aLookahead ) lemp->aLookahead[i] = lemp->nterminal;
    if( j==0 ) fprintf(out," /* %5d */ ", i);
    fprintf(out, " %4d,", lemp->nterminal);
    if( j==9 ){
      fprintf(out, "\n"); lineno++;
      j = 0;
    }else{
      j++;
    }
    i++;
  }
  if( j>0 ){ fprintf(out, "\n"); lineno++; }
  fprintf(out, "};\n"); lineno++;

  /* Output the yy_shift_ofst[] table */
  n = lemp->nxstate;
  while( n>0 && lemp->sorted[n-1]->iTknOfst==NO_OFFSET ) n--;
  fprintf(out, "#define YY_SHIFT_COUNT    (%d)\n", n-1); lineno++;
  fprintf(out, "#define YY_SHIFT_MIN      (%d)\n", mnTknOfst); lineno++;
  fprintf(out, "#define YY_SHIFT_MAX      (%d)\n", mxTknOfst); lineno++;
  fprintf(out, "static const %s yy_shift_ofst[] = {\n",
       minimum_size_type(mnTknOfst, lemp->nterminal+lemp->nactiontab, &sz));
       lineno++;
  lemp->tablesize += n*sz;
  for(i=j=0; i<n; i++){
    int ofst;
    stp = lemp->sorted[i];
    ofst = stp->iTknOfst;
    if( ofst==NO_OFFSET ) ofst = lemp->nactiontab;
    if( j==0 ) fprintf(out," /* %5d */ ", i);
    fprintf(out, " %4d,", ofst);
    if( j==9 || i==n-1 ){
      fprintf(out, "\n"); lineno++;
      j = 0;
    }else{
      j++;
    }
  }
  fprintf(out, "};\n"); lineno++;

  /* Output the yy_reduce_ofst[] table */
  n = lemp->nxstate;
  while( n>0 && lemp->sorted[n-1]->iNtOfst==NO_OFFSET ) n--;
  fprintf(out, "#define YY_REDUCE_COUNT (%d)\n", n-1); lineno++;
  fprintf(out, "#define YY_REDUCE_MIN   (%d)\n", mnNtOfst); lineno++;
  fprintf(out, "#define YY_REDUCE_MAX   (%d)\n", mxNtOfst); lineno++;
  fprintf(out, "static const %s yy_reduce_ofst[] = {\n",
          minimum_size_type(mnNtOfst-1, mxNtOfst, &sz)); lineno++;
  lemp->tablesize += n*sz;
  for(i=j=0; i<n; i++){
    int ofst;
    stp = lemp->sorted[i];
    ofst = stp->iNtOfst;
    if( ofst==NO_OFFSET ) ofst = mnNtOfst - 1;
    if( j==0 ) fprintf(out," /* %5d */ ", i);
    fprintf(out, " %4d,", ofst);
    if( j==9 || i==n-1 ){
      fprintf(out, "\n"); lineno++;
      j = 0;
    }else{
      j++;
    }
  }
  fprintf(out, "};\n"); lineno++;

  /* Output the default action table */
  fprintf(out, "static const YYACTIONTYPE yy_default[] = {\n"); lineno++;
  n = lemp->nxstate;
  lemp->tablesize += n*szActionType;
  for(i=j=0; i<n; i++){
    stp = lemp->sorted[i];
    if( j==0 ) fprintf(out," /* %5d */ ", i);
    if( stp->iDfltReduce<0 ){
      fprintf(out, " %4d,", lemp->errAction);
    }else{
      fprintf(out, " %4d,", stp->iDfltReduce + lemp->minReduce);
    }
    if( j==9 || i==n-1 ){
      fprintf(out, "\n"); lineno++;
      j = 0;
    }else{
      j++;
    }
  }
  fprintf(out, "};\n"); lineno++;
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate the table of fallback tokens.
  */
  if( lemp->has_fallback ){
    mx = lemp->nterminal - 1;
    /* 2019-08-28:  Generate fallback entries for every token to avoid
    ** having to do a range check on the index */
    /* while( mx>0 && lemp->symbols[mx]->fallback==0 ){ mx--; } */
    lemp->tablesize += (mx+1)*szCodeType;
    for(i=0; i<=mx; i++){
      struct symbol *p = lemp->symbols[i];
      if( p->fallback==0 ){
        fprintf(out, "    0,  /* %10s => nothing */\n", p->name);
      }else{
        fprintf(out, "  %3d,  /* %10s => %s */\n", p->fallback->index,
          p->name, p->fallback->name);
      }
      lineno++;
    }
  }
  tplt_xfer(lemp->name, in, out, &lineno);

  /* Generate a table containing the symbolic name of every symbol
  */
  for(i=0; i<lemp->nsymbol; i++){
    fprintf(out,"  /* %4d */ \"%s\",\n",i, lemp->symbols[i]->name); lineno++;
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate a table containing a text string that describes every
  ** rule in the rule set of the grammar.  This information is used
  ** when tracing REDUCE actions.
  */
  for(i=0, rp=lemp->rule; rp; rp=rp->next, i++){
    assert( rp->iRule==i );
    fprintf(out," /* %3d */ \"", i);
    writeRuleText(out, rp);
    fprintf(out,"\",\n"); lineno++;
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which executes every time a symbol is popped from
  ** the stack while processing errors or while destroying the parser.
  ** (In other words, generate the %destructor actions)
  */
  if( lemp->tokendest ){
    int once = 1;
    for(i=0; i<lemp->nsymbol; i++){
      struct symbol *sp = lemp->symbols[i];
      if( sp==0 || sp->type!=TERMINAL ) continue;
      if( once ){
        fprintf(out, "      /* TERMINAL Destructor */\n"); lineno++;
        once = 0;
      }
      fprintf(out,"    case %d: /* %s */\n", sp->index, sp->name); lineno++;
    }
    for(i=0; i<lemp->nsymbol && lemp->symbols[i]->type!=TERMINAL; i++);
    if( i<lemp->nsymbol ){
      emit_destructor_code(out,lemp->symbols[i],lemp,&lineno);
      fprintf(out,"      break;\n"); lineno++;
    }
  }
  if( lemp->vardest ){
    struct symbol *dflt_sp = 0;
    int once = 1;
    for(i=0; i<lemp->nsymbol; i++){
      struct symbol *sp = lemp->symbols[i];
      if( sp==0 || sp->type==TERMINAL ||
          sp->index<=0 || sp->destructor!=0 ) continue;
      if( once ){
        fprintf(out, "      /* Default NON-TERMINAL Destructor */\n");lineno++;
        once = 0;
      }
      fprintf(out,"    case %d: /* %s */\n", sp->index, sp->name); lineno++;
      dflt_sp = sp;
    }
    if( dflt_sp!=0 ){
      emit_destructor_code(out,dflt_sp,lemp,&lineno);
    }
    fprintf(out,"      break;\n"); lineno++;
  }
  for(i=0; i<lemp->nsymbol; i++){
    struct symbol *sp = lemp->symbols[i];
    if( sp==0 || sp->type==TERMINAL || sp->destructor==0 ) continue;
    if( sp->destLineno<0 ) continue;  /* Already emitted */
    fprintf(out,"    case %d: /* %s */\n", sp->index, sp->name); lineno++;

    /* Combine duplicate destructors into a single case */
    for(j=i+1; j<lemp->nsymbol; j++){
      struct symbol *sp2 = lemp->symbols[j];
      if( sp2 && sp2->type!=TERMINAL && sp2->destructor
          && sp2->dtnum==sp->dtnum
          && strcmp(sp->destructor,sp2->destructor)==0 ){
         fprintf(out,"    case %d: /* %s */\n",
                 sp2->index, sp2->name); lineno++;
         sp2->destLineno = -1;  /* Avoid emitting this destructor again */
      }
    }
    emit_destructor_code(out,lemp->symbols[i],lemp,&lineno);
    fprintf(out,"      break;\n"); lineno++;
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which executes whenever the parser stack overflows */
  tplt_print(out,lemp,lemp->overflow,&lineno);
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate the tables of rule information.  yyRuleInfoLhs[] and
  ** yyRuleInfoNRhs[].
  **
  ** Note: This code depends on the fact that rules are number
  ** sequentially beginning with 0.
  */
  for(i=0, rp=lemp->rule; rp; rp=rp->next, i++){
    fprintf(out,"  %4d,  /* (%d) ", rp->lhs->index, i);
     rule_print(out, rp);
    fprintf(out," */\n"); lineno++;
  }
  tplt_xfer(lemp->name,in,out,&lineno);
  for(i=0, rp=lemp->rule; rp; rp=rp->next, i++){
    fprintf(out,"  %3d,  /* (%d) ", -rp->nrhs, i);
    rule_print(out, rp);
    fprintf(out," */\n"); lineno++;
  }
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which execution during each REDUCE action */
  i = 0;
  for(rp=lemp->rule; rp; rp=rp->next){
    i += translate_code(lemp, rp);
  }
  if( i ){
    fprintf(out,"        YYMINORTYPE yylhsminor;\n"); lineno++;
  }
  /* First output rules other than the default: rule */
  for(rp=lemp->rule; rp; rp=rp->next){
    struct rule *rp2;               /* Other rules with the same action */
    if( rp->codeEmitted ) continue;
    if( rp->noCode ){
      /* No C code actions, so this will be part of the "default:" rule */
      continue;
    }
    fprintf(out,"      case %d: /* ", rp->iRule);
    writeRuleText(out, rp);
    fprintf(out, " */\n"); lineno++;
    for(rp2=rp->next; rp2; rp2=rp2->next){
      if( rp2->code==rp->code && rp2->codePrefix==rp->codePrefix
             && rp2->codeSuffix==rp->codeSuffix ){
        fprintf(out,"      case %d: /* ", rp2->iRule);
        writeRuleText(out, rp2);
        fprintf(out," */ yytestcase(yyruleno==%d);\n", rp2->iRule); lineno++;
        rp2->codeEmitted = 1;
      }
    }
    emit_code(out,rp,lemp,&lineno);
    fprintf(out,"        break;\n"); lineno++;
    rp->codeEmitted = 1;
  }
  /* Finally, output the default: rule.  We choose as the default: all
  ** empty actions. */
  fprintf(out,"      default:\n"); lineno++;
  for(rp=lemp->rule; rp; rp=rp->next){
    if( rp->codeEmitted ) continue;
    assert( rp->noCode );
    fprintf(out,"      /* (%d) ", rp->iRule);
    writeRuleText(out, rp);
    if( rp->neverReduce ){
      fprintf(out, " (NEVER REDUCES) */ assert(yyruleno!=%d);\n",
              rp->iRule); lineno++;
    }else if( rp->doesReduce ){
      fprintf(out, " */ yytestcase(yyruleno==%d);\n", rp->iRule); lineno++;
    }else{
      fprintf(out, " (OPTIMIZED OUT) */ assert(yyruleno!=%d);\n",
              rp->iRule); lineno++;
    }
  }
  fprintf(out,"        break;\n"); lineno++;
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which executes if a parse fails */
  tplt_print(out,lemp,lemp->failure,&lineno);
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which executes when a syntax error occurs */
  tplt_print(out,lemp,lemp->error,&lineno);
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Generate code which executes when the parser accepts its input */
  tplt_print(out,lemp,lemp->accept,&lineno);
  tplt_xfer(lemp->name,in,out,&lineno);

  /* Append any addition code the user desires */
  tplt_print(out,lemp,lemp->extracode,&lineno);

  acttab_free(pActtab);
  fclose(in);
  fclose(out);
  return;
}

/* Generate a header file for the parser */
void ReportHeader(struct lime *lemp)
{
  FILE *out, *in;
  const char *prefix;
  char line[LINESIZE];
  char pattern[LINESIZE];
  int i;

  if( lemp->tokenprefix ) prefix = lemp->tokenprefix;
  else                    prefix = "";
  /* Skip change-detection when AST nodes are present (header has complex
  ** content beyond simple #define lines) */
  if( !lemp->ast_nodes ){
    in = file_open(lemp,".h","rb");
    if( in ){
      int nextChar;
      for(i=1; i<lemp->nterminal && fgets(line,LINESIZE,in); i++){
        lemon_sprintf(pattern,"#define %s%-30s %3d\n",
                      prefix,lemp->symbols[i]->name,i+lemp->first_token);
        if( strcmp(line,pattern) ) break;
      }
      nextChar = fgetc(in);
      fclose(in);
      if( i==lemp->nterminal && nextChar==EOF ){
        /* No change in the file.  Don't rewrite it. */
        return;
      }
    }
  }
  out = file_open(lemp,".h","wb");
  if( out ){
    for(i=1; i<lemp->nterminal; i++){
      fprintf(out,"#define %s%-30s %3d\n",prefix,lemp->symbols[i]->name,
              i+lemp->first_token);
    }

    /* Generate AST type definitions if %ast_node directives were used */
    if( lemp->ast_nodes ){
      const char *ap = lemp->ast_prefix ? lemp->ast_prefix : "";
      struct ast_node_def *nd;
      int tag;

      fprintf(out,"\n/* AST node type tags (generated by Lime %%ast_node) */\n");
      fprintf(out,"#ifndef LIME_AST_TYPES_DEFINED\n");
      fprintf(out,"#define LIME_AST_TYPES_DEFINED\n");
      fprintf(out,"#include \"lime_ast.h\"\n\n");

      /* Assign tag values and emit enum */
      tag = 1;
      for(nd=lemp->ast_nodes; nd; nd=nd->next){
        nd->tag = tag++;
      }
      fprintf(out,"enum %sNodeTag {\n", ap);
      for(nd=lemp->ast_nodes; nd; nd=nd->next){
        fprintf(out,"  %s%s_TAG = %d%s\n", ap, nd->name, nd->tag,
                nd->next ? "," : "");
      }
      fprintf(out,"};\n\n");

      /* Forward declarations */
      for(nd=lemp->ast_nodes; nd; nd=nd->next){
        fprintf(out,"typedef struct %s%s %s%s;\n", ap, nd->name, ap, nd->name);
      }
      fprintf(out,"\n");

      /* Struct definitions */
      for(nd=lemp->ast_nodes; nd; nd=nd->next){
        if( nd->is_list ){
          /* List node: contains an array of element pointers */
          fprintf(out,"struct %s%s {\n", ap, nd->name);
          fprintf(out,"  int tag;\n");
          fprintf(out,"  %s%s **items;\n", ap, nd->element_type);
          fprintf(out,"  int count;\n");
          fprintf(out,"  int capacity;\n");
          fprintf(out,"};\n\n");
        }else{
          /* Regular node with user-defined fields */
          struct ast_field *fld;
          fprintf(out,"struct %s%s {\n", ap, nd->name);
          fprintf(out,"  int tag;\n");
          for(fld=nd->fields; fld; fld=fld->next){
            fprintf(out,"  %s %s;\n", fld->type, fld->name);
          }
          fprintf(out,"};\n\n");
        }
      }

      /* Constructor prototypes */
      for(nd=lemp->ast_nodes; nd; nd=nd->next){
        if( nd->is_list ){
          fprintf(out,"%s%s *%s_new_%s(LimeArena *a);\n",
                  ap, nd->name, ap, nd->name);
          fprintf(out,"void %s_%s_append(LimeArena *a, %s%s *list, %s%s *item);\n",
                  ap, nd->name, ap, nd->name, ap, nd->element_type);
        }else{
          struct ast_field *fld;
          fprintf(out,"%s%s *%s_new_%s(LimeArena *a",
                  ap, nd->name, ap, nd->name);
          for(fld=nd->fields; fld; fld=fld->next){
            fprintf(out,", %s %s", fld->type, fld->name);
          }
          fprintf(out,");\n");
        }
      }

      /* AST_NEW convenience macro */
      fprintf(out,"\n#define AST_NEW(arena, Type, ...) "
              "%s_new_##Type(arena, ##__VA_ARGS__)\n", ap);

      fprintf(out,"\n#endif /* LIME_AST_TYPES_DEFINED */\n");
    }
    fclose(out);
  }
  return;
}

/*
** Generate a *_snapshot.c file that contains a per-grammar
** <Prefix>BuildSnapshot() implementation.  This function allocates a
** ParserSnapshot struct and populates it with the action tables and
** rule metadata computed by ReportTable() / FindActions(), so the
** runtime push parser (parse_token) can drive this grammar without
** going through the generator's static entry point.
**
** The emitted symbol is named <Prefix>BuildSnapshot, where <Prefix>
** comes from %name (defaults to "Parse").  Multiple grammars can be
** snapshot-emitted into the same binary without colliding.
**
** Applications can also call lime_snapshot_create("foo.y", &err) at
** runtime; that path relies on the build system having emitted a
** matching <Prefix>BuildSnapshot earlier so the runtime can dispatch
** by grammar name.
*/
void ReportSnapshotInit(struct lime *lemp)
{
  FILE *out;
  int i, j, n;
  struct state *stp;
  int nLookAhead;
  const char *name = lemp->name ? lemp->name : "Parse";

  out = file_open(lemp,"_snapshot.c","wb");
  if( out==0 ) return;

  fprintf(out,
    "/*\n"
    "** Snapshot initialization code generated by Lime from \"%s\".\n"
    "** Do not edit -- this file is machine-generated.\n"
    "**\n"
    "** Provides %sBuildSnapshot() which returns a ParserSnapshot\n"
    "** populated with the action tables and rule metadata for this\n"
    "** grammar.  The runtime push parser (parse_token) drives the\n"
    "** returned snapshot without going through the static %s()\n"
    "** entry point.\n"
    "*/\n"
    "#ifndef _POSIX_C_SOURCE\n"
    "#define _POSIX_C_SOURCE 199309L\n"
    "#endif\n"
    "#include \"snapshot.h\"\n"
    "#include \"snapshot_build.h\"\n"
    "#include <stdlib.h>\n"
    "#include <string.h>\n"
    "#include <stdatomic.h>\n"
    "#include <time.h>\n"
    "\n",
    lemp->filename, name, name
  );

  /* ------------------------------------------------------------ */
  /*  Static tables                                               */
  /* ------------------------------------------------------------ */

  /* yy_action */
  n = lemp->nactiontab;
  fprintf(out, "static const uint16_t s_yy_action[] = {\n");
  for(i=j=0; i<n; i++){
    int action = lemp->aAction ? lemp->aAction[i] : 0;
    if( j==0 ) fprintf(out,"    ");
    fprintf(out, "%d,", action);
    if( j==9 || i==n-1 ){ fprintf(out, "\n"); j = 0; }
    else j++;
  }
  fprintf(out, "};\n");

  /* yy_lookahead */
  nLookAhead = lemp->nLookahead;
  fprintf(out, "static const uint16_t s_yy_lookahead[] = {\n");
  for(i=j=0; i<nLookAhead; i++){
    int la = lemp->aLookahead ? lemp->aLookahead[i] : 0;
    if( j==0 ) fprintf(out,"    ");
    fprintf(out, "%d,", la);
    if( j==9 || i==nLookAhead-1 ){ fprintf(out, "\n"); j = 0; }
    else j++;
  }
  fprintf(out, "};\n");

  /* yy_shift_ofst */
  n = lemp->nxstate;
  fprintf(out, "static const int32_t s_yy_shift_ofst[] = {\n");
  for(i=j=0; i<n; i++){
    int ofst;
    stp = lemp->sorted[i];
    ofst = stp->iTknOfst;
    if( ofst==NO_OFFSET ) ofst = lemp->nactiontab;
    if( j==0 ) fprintf(out,"    ");
    fprintf(out, "%d,", ofst);
    if( j==9 || i==n-1 ){ fprintf(out, "\n"); j = 0; }
    else j++;
  }
  fprintf(out, "};\n");

  /* yy_reduce_ofst */
  {
    int mnNtOfst = 0;
    for(i=0; i<lemp->nxstate; i++){
      stp = lemp->sorted[i];
      if( stp->iNtOfst!=NO_OFFSET && stp->iNtOfst<mnNtOfst ){
        mnNtOfst = stp->iNtOfst;
      }
    }
    fprintf(out, "static const int32_t s_yy_reduce_ofst[] = {\n");
    for(i=j=0; i<n; i++){
      int ofst;
      stp = lemp->sorted[i];
      ofst = stp->iNtOfst;
      if( ofst==NO_OFFSET ) ofst = mnNtOfst - 1;
      if( j==0 ) fprintf(out,"    ");
      fprintf(out, "%d,", ofst);
      if( j==9 || i==n-1 ){ fprintf(out, "\n"); j = 0; }
      else j++;
    }
    fprintf(out, "};\n");
  }

  /* yy_default */
  fprintf(out, "static const uint16_t s_yy_default[] = {\n");
  for(i=j=0; i<n; i++){
    stp = lemp->sorted[i];
    int dflt;
    if( stp->iDfltReduce<0 ){
      dflt = lemp->errAction;
    }else{
      dflt = stp->iDfltReduce + lemp->minReduce;
    }
    if( j==0 ) fprintf(out,"    ");
    fprintf(out, "%d,", dflt);
    if( j==9 || i==n-1 ){ fprintf(out, "\n"); j = 0; }
    else j++;
  }
  fprintf(out, "};\n");

  /* Rule info: LHS symbol code per rule */
  fprintf(out, "static const int16_t s_yy_rule_info_lhs[] = {\n");
  {
    struct rule *rp;
    for(rp=lemp->rule, i=j=0; rp; rp=rp->next, i++){
      int lhs = rp->lhs ? rp->lhs->index : 0;
      if( j==0 ) fprintf(out, "    ");
      fprintf(out, "%d,", lhs);
      if( j==9 || rp->next==NULL ){ fprintf(out, "\n"); j = 0; }
      else j++;
    }
    if( i==0 ) fprintf(out, "    0\n");
  }
  fprintf(out, "};\n");

  /* Rule info: -nrhs per rule (negative count, matching limpar.c). */
  fprintf(out, "static const int8_t s_yy_rule_info_nrhs[] = {\n");
  {
    struct rule *rp;
    for(rp=lemp->rule, i=j=0; rp; rp=rp->next, i++){
      int nrhs = -rp->nrhs;
      if( j==0 ) fprintf(out, "    ");
      fprintf(out, "%d,", nrhs);
      if( j==9 || rp->next==NULL ){ fprintf(out, "\n"); j = 0; }
      else j++;
    }
    if( i==0 ) fprintf(out, "    0\n");
  }
  fprintf(out, "};\n");

  /* ------------------------------------------------------------ */
  /*  Grammar source text                                         */
  /* ------------------------------------------------------------ */
  /*
  ** Emit the original grammar's source text as a static byte
  ** array.  The runtime extension framework needs this to rebuild
  ** the LALR(1) automaton when an extension adds tokens or rules:
  ** publish_modified_snapshot() concatenates the original text
  ** with lime_modifications_to_grammar_text(mods) and reruns the
  ** subprocess pipeline (lime + cc) on the merged grammar.
  **
  ** Emit as a `static const unsigned char[]` rather than a string
  ** literal so we don't hit the C99 4095-byte string-literal length
  ** limit on large grammars (e.g. the PostgreSQL grammar at
  ** ~525,000 bytes).  Trailing NUL is appended so the runtime can
  ** treat it as a C string.
  */
  fprintf(out, "static const unsigned char s_grammar_source[] = {\n");
  {
    FILE *src_in = fopen(lemp->filename, "rb");
    int col = 0;
    if (src_in != NULL) {
      int c;
      while ((c = fgetc(src_in)) != EOF) {
        if (col == 0) fprintf(out, "    ");
        fprintf(out, "0x%02x,", (unsigned char)c);
        col++;
        if (col >= 16) {
          fprintf(out, "\n");
          col = 0;
        }
      }
      fclose(src_in);
      if (col > 0) fprintf(out, "\n");
    }
  }
  /* Trailing NUL so consumers can treat the array as a C string. */
  fprintf(out, "    0x00\n};\n");

  /* ------------------------------------------------------------ */
  /*  Builder function                                            */
  /* ------------------------------------------------------------ */

  fprintf(out, "\nParserSnapshot *%sBuildSnapshot(void) {\n", name);
  fprintf(out,
    "    LimeParserTables tables = {\n"
    "        .yy_action            = s_yy_action,\n"
    "        .yy_action_count      = sizeof(s_yy_action)/sizeof(s_yy_action[0]),\n"
    "        .yy_lookahead         = s_yy_lookahead,\n"
    "        .yy_lookahead_count   = sizeof(s_yy_lookahead)/sizeof(s_yy_lookahead[0]),\n"
    "        .yy_shift_ofst        = s_yy_shift_ofst,\n"
    "        .yy_reduce_ofst       = s_yy_reduce_ofst,\n"
    "        .yy_default           = s_yy_default,\n"
    "        .nstate               = sizeof(s_yy_default)/sizeof(s_yy_default[0]),\n"
    "        .yy_rule_info_lhs     = s_yy_rule_info_lhs,\n"
    "        .yy_rule_info_nrhs    = s_yy_rule_info_nrhs,\n"
    "        .nrule                = %d,\n"
    "        .nsymbol              = %d,\n"
    "        .nterminal            = %d,\n"
    "        .ntoken               = %d,\n"
    "        .yy_max_shift         = %d,\n"
    "        .yy_min_shiftreduce   = %d,\n"
    "        .yy_max_shiftreduce   = %d,\n"
    "        .yy_error_action      = %d,\n"
    "        .yy_accept_action     = %d,\n"
    "        .yy_no_action         = %d,\n"
    "        .yy_min_reduce        = %d,\n"
    "        .yy_fallback          = NULL,\n"
    "        .nfallback            = 0,\n"
    "        .grammar_source       = (const char *)s_grammar_source,\n"
    "        .grammar_source_len   = sizeof(s_grammar_source) - 1,\n"
    "    };\n"
    "    return snapshot_build_from_tables(&tables);\n"
    "}\n"
    "\n"
    "/*\n"
    "** Generic entry-point alias used by lime_snapshot_create() at\n"
    "** runtime.  Each generated snapshot file emits this symbol so a\n"
    "** caller that has just dlopen()ed the .so can resolve the\n"
    "** snapshot builder via dlsym(handle, \"lime_snapshot_entry\")\n"
    "** without first knowing the grammar's %%name prefix.\n"
    "**\n"
    "** When more than one *_snapshot.c is linked into the same image,\n"
    "** the linker rejects duplicate definitions of\n"
    "** lime_snapshot_entry; that is intentional -- the runtime\n"
    "** registry is one snapshot per loaded module, and statically\n"
    "** linking two grammars together is the wrong shape.  Use\n"
    "** dlopen() per grammar instead.\n"
    "*/\n"
    "#ifndef LIME_NO_SNAPSHOT_ENTRY_ALIAS\n"
    "ParserSnapshot *lime_snapshot_entry(void) {\n"
    "    return %sBuildSnapshot();\n"
    "}\n"
    "#endif\n",
    lemp->nrule,
    lemp->nsymbol,
    lemp->nterminal,
    lemp->nterminal,                            /* YYNTOKEN = nterminal */
    lemp->nxstate - 1,                          /* YY_MAX_SHIFT */
    lemp->minShiftReduce,                       /* YY_MIN_SHIFTREDUCE */
    lemp->minShiftReduce + lemp->nrule - 1,     /* YY_MAX_SHIFTREDUCE */
    lemp->errAction,
    lemp->accAction,
    lemp->noAction,
    lemp->minReduce,
    name
  );

  fclose(out);
}

/* Reduce the size of the action tables, if possible, by making use
** of defaults.
**
** In this version, we take the most frequent REDUCE action and make
** it the default.  Except, there is no default if the wildcard token
** is a possible look-ahead.
*/
void CompressTables(struct lime *lemp)
{
  struct state *stp;
  struct action *ap, *ap2, *nextap;
  struct rule *rp, *rp2, *rbest;
  int nbest, n;
  int i;
  int usesWildcard;

  for(i=0; i<lemp->nstate; i++){
    stp = lemp->sorted[i];
    nbest = 0;
    rbest = 0;
    usesWildcard = 0;

    for(ap=stp->ap; ap; ap=ap->next){
      if( ap->type==SHIFT && ap->sp==lemp->wildcard ){
        usesWildcard = 1;
      }
      if( ap->type!=REDUCE ) continue;
      rp = ap->x.rp;
      if( rp->lhsStart ) continue;
      if( rp==rbest ) continue;
      n = 1;
      for(ap2=ap->next; ap2; ap2=ap2->next){
        if( ap2->type!=REDUCE ) continue;
        rp2 = ap2->x.rp;
        if( rp2==rbest ) continue;
        if( rp2==rp ) n++;
      }
      if( n>nbest ){
        nbest = n;
        rbest = rp;
      }
    }

    /* Do not make a default if the number of rules to default
    ** is not at least 1 or if the wildcard token is a possible
    ** lookahead.
    */
    if( nbest<1 || usesWildcard ) continue;


    /* Combine matching REDUCE actions into a single default */
    for(ap=stp->ap; ap; ap=ap->next){
      if( ap->type==REDUCE && ap->x.rp==rbest ) break;
    }
    assert( ap );
    ap->sp = Symbol_new("{default}");
    for(ap=ap->next; ap; ap=ap->next){
      if( ap->type==REDUCE && ap->x.rp==rbest ) ap->type = NOT_USED;
    }
    stp->ap = Action_sort(stp->ap);

    for(ap=stp->ap; ap; ap=ap->next){
      if( ap->type==SHIFT ) break;
      if( ap->type==REDUCE && ap->x.rp!=rbest ) break;
    }
    if( ap==0 ){
      stp->autoReduce = 1;
      stp->pDfltReduce = rbest;
    }
  }

  /* Make a second pass over all states and actions.  Convert
  ** every action that is a SHIFT to an autoReduce state into
  ** a SHIFTREDUCE action.
  */
  for(i=0; i<lemp->nstate; i++){
    stp = lemp->sorted[i];
    for(ap=stp->ap; ap; ap=ap->next){
      struct state *pNextState;
      if( ap->type!=SHIFT ) continue;
      pNextState = ap->x.stp;
      if( pNextState->autoReduce && pNextState->pDfltReduce!=0 ){
        ap->type = SHIFTREDUCE;
        ap->x.rp = pNextState->pDfltReduce;
      }
    }
  }

  /* If a SHIFTREDUCE action specifies a rule that has a single RHS term
  ** (meaning that the SHIFTREDUCE will land back in the state where it
  ** started) and if there is no C-code associated with the reduce action,
  ** then we can go ahead and convert the action to be the same as the
  ** action for the RHS of the rule.
  */
  for(i=0; i<lemp->nstate; i++){
    stp = lemp->sorted[i];
    for(ap=stp->ap; ap; ap=nextap){
      nextap = ap->next;
      if( ap->type!=SHIFTREDUCE ) continue;
      rp = ap->x.rp;
      if( rp->noCode==0 ) continue;
      if( rp->nrhs!=1 ) continue;
#if 1
      /* Only apply this optimization to non-terminals.  It would be OK to
      ** apply it to terminal symbols too, but that makes the parser tables
      ** larger. */
      if( ap->sp->index<lemp->nterminal ) continue;
#endif
      /* If we reach this point, it means the optimization can be applied */
      nextap = ap;
      for(ap2=stp->ap; ap2 && (ap2==ap || ap2->sp!=rp->lhs); ap2=ap2->next){}
      assert( ap2!=0 );
      ap->spOpt = ap2->sp;
      ap->type = ap2->type;
      ap->x = ap2->x;
    }
  }
}


/*
** Compare two states for sorting purposes.  The smaller state is the
** one with the most non-terminal actions.  If they have the same number
** of non-terminal actions, then the smaller is the one with the most
** token actions.
*/
static int stateResortCompare(const void *a, const void *b){
  const struct state *pA = *(const struct state**)a;
  const struct state *pB = *(const struct state**)b;
  int n;

  n = pB->nNtAct - pA->nNtAct;
  if( n==0 ){
    n = pB->nTknAct - pA->nTknAct;
    if( n==0 ){
      n = pB->statenum - pA->statenum;
    }
  }
  assert( n!=0 );
  return n;
}


/*
** Renumber and resort states so that states with fewer choices
** occur at the end.  Except, keep state 0 as the first state.
*/
void ResortStates(struct lime *lemp)
{
  int i;
  struct state *stp;
  struct action *ap;

  for(i=0; i<lemp->nstate; i++){
    stp = lemp->sorted[i];
    stp->nTknAct = stp->nNtAct = 0;
    stp->iDfltReduce = -1; /* Init dflt action to "syntax error" */
    stp->iTknOfst = NO_OFFSET;
    stp->iNtOfst = NO_OFFSET;
    for(ap=stp->ap; ap; ap=ap->next){
      int iAction = compute_action(lemp,ap);
      if( iAction>=0 ){
        if( ap->sp->index<lemp->nterminal ){
          stp->nTknAct++;
        }else if( ap->sp->index<lemp->nsymbol ){
          stp->nNtAct++;
        }else{
          assert( stp->autoReduce==0 || stp->pDfltReduce==ap->x.rp );
          stp->iDfltReduce = iAction;
        }
      }
    }
  }
  qsort(&lemp->sorted[1], lemp->nstate-1, sizeof(lemp->sorted[0]),
        stateResortCompare);
  for(i=0; i<lemp->nstate; i++){
    lemp->sorted[i]->statenum = i;
  }
  lemp->nxstate = lemp->nstate;
  while( lemp->nxstate>1 && lemp->sorted[lemp->nxstate-1]->autoReduce ){
    lemp->nxstate--;
  }
}


/***************** From the file "set.c" ************************************/
/*
** Set manipulation routines for the LEMON parser generator.
*/

static int size = 0;

/* Set the set size */
void SetSize(int n)
{
  size = n+1;
}

/* Allocate a new set */
char *SetNew(void){
  char *s;
  s = (char*)lime_calloc( size, 1);
  if( s==0 ){
    memory_error();
  }
  return s;
}

/* Deallocate a set */
void SetFree(char *s)
{
  lime_free(s);
}

/* Add a new element to the set.  Return TRUE if the element was added
** and FALSE if it was already there. */
int SetAdd(char *s, int e)
{
  int rv;
  assert( e>=0 && e<size );
  rv = s[e];
  s[e] = 1;
  return !rv;
}

/* Add every element of s2 to s1.  Return TRUE if s1 changes. */
int SetUnion(char *s1, char *s2)
{
  int i, progress;
  progress = 0;
  for(i=0; i<size; i++){
    if( s2[i]==0 ) continue;
    if( s1[i]==0 ){
      progress = 1;
      s1[i] = 1;
    }
  }
  return progress;
}
/********************** From the file "table.c" ****************************/
/*
** All code in this file has been automatically generated
** from a specification in the file
**              "table.q"
** by the associative array code building program "aagen".
** Do not edit this file!  Instead, edit the specification
** file, then rerun aagen.
*/
/*
** Code for processing tables in the LEMON parser generator.
*/

PRIVATE unsigned strhash(const char *x)
{
  unsigned h = 0;
  while( *x ) h = h*13 + *(x++);
  return h;
}

/* Works like strdup, sort of.  Save a string in malloced memory, but
** keep strings in a table so that the same string is not in more
** than one place.
*/
const char *Strsafe(const char *y)
{
  const char *z;
  char *cpy;

  if( y==0 ) return 0;
  z = Strsafe_find(y);
  if( z==0 && (cpy=(char *)lime_malloc( lemonStrlen(y)+1 ))!=0 ){
    lemon_strcpy(cpy,y);
    z = cpy;
    Strsafe_insert(z);
  }
  MemoryCheck(z);
  return z;
}

/* There is one instance of the following structure for each
** associative array of type "x1".
*/
struct s_x1 {
  int size;               /* The number of available slots. */
                          /*   Must be a power of 2 greater than or */
                          /*   equal to 1 */
  int count;              /* Number of currently slots filled */
  struct s_x1node *tbl;  /* The data stored here */
  struct s_x1node **ht;  /* Hash table for lookups */
};

/* There is one instance of this structure for every data element
** in an associative array of type "x1".
*/
typedef struct s_x1node {
  const char *data;        /* The data */
  struct s_x1node *next;   /* Next entry with the same hash */
  struct s_x1node **from;  /* Previous link */
} x1node;

/* There is only one instance of the array, which is the following */
static struct s_x1 *x1a;

/* Allocate a new associative array */
void Strsafe_init(void){
  if( x1a ) return;
  x1a = (struct s_x1*)lime_malloc( sizeof(struct s_x1) );
  if( x1a ){
    x1a->size = 1024;
    x1a->count = 0;
    x1a->tbl = (x1node*)lime_calloc(1024, sizeof(x1node) + sizeof(x1node*));
    if( x1a->tbl==0 ){
      lime_free(x1a);
      x1a = 0;
    }else{
      int i;
      x1a->ht = (x1node**)&(x1a->tbl[1024]);
      for(i=0; i<1024; i++) x1a->ht[i] = 0;
    }
  }
}
/* Insert a new record into the array.  Return TRUE if successful.
** Prior data with the same key is NOT overwritten */
int Strsafe_insert(const char *data)
{
  x1node *np;
  unsigned h;
  unsigned ph;

  if( x1a==0 ) return 0;
  ph = strhash(data);
  h = ph & (x1a->size-1);
  np = x1a->ht[h];
  while( np ){
    if( strcmp(np->data,data)==0 ){
      /* An existing entry with the same key is found. */
      /* Fail because overwrite is not allows. */
      return 0;
    }
    np = np->next;
  }
  if( x1a->count>=x1a->size ){
    /* Need to make the hash table bigger */
    int i,arrSize;
    struct s_x1 array;
    array.size = arrSize = x1a->size*2;
    array.count = x1a->count;
    array.tbl = (x1node*)lime_calloc(arrSize, sizeof(x1node)+sizeof(x1node*));
    if( array.tbl==0 ) return 0;  /* Fail due to malloc failure */
    array.ht = (x1node**)&(array.tbl[arrSize]);
    for(i=0; i<arrSize; i++) array.ht[i] = 0;
    for(i=0; i<x1a->count; i++){
      x1node *oldnp, *newnp;
      oldnp = &(x1a->tbl[i]);
      h = strhash(oldnp->data) & (arrSize-1);
      newnp = &(array.tbl[i]);
      if( array.ht[h] ) array.ht[h]->from = &(newnp->next);
      newnp->next = array.ht[h];
      newnp->data = oldnp->data;
      newnp->from = &(array.ht[h]);
      array.ht[h] = newnp;
    }
    /* lime_free(x1a->tbl); // This program was originally for 16-bit machines.
    ** Don't worry about freeing memory on modern platforms. */
    *x1a = array;
  }
  /* Insert the new data */
  h = ph & (x1a->size-1);
  np = &(x1a->tbl[x1a->count++]);
  np->data = data;
  if( x1a->ht[h] ) x1a->ht[h]->from = &(np->next);
  np->next = x1a->ht[h];
  x1a->ht[h] = np;
  np->from = &(x1a->ht[h]);
  return 1;
}

/* Return a pointer to data assigned to the given key.  Return NULL
** if no such key. */
const char *Strsafe_find(const char *key)
{
  unsigned h;
  x1node *np;

  if( x1a==0 ) return 0;
  h = strhash(key) & (x1a->size-1);
  np = x1a->ht[h];
  while( np ){
    if( strcmp(np->data,key)==0 ) break;
    np = np->next;
  }
  return np ? np->data : 0;
}

/* Return a pointer to the (terminal or nonterminal) symbol "x".
** Create a new symbol if this is the first time "x" has been seen.
*/
struct symbol *Symbol_new(const char *x)
{
  struct symbol *sp;

  sp = Symbol_find(x);
  if( sp==0 ){
    sp = (struct symbol *)lime_calloc(1, sizeof(struct symbol) );
    MemoryCheck(sp);
    sp->name = Strsafe(x);
    sp->type = ISUPPER(*x) ? TERMINAL : NONTERMINAL;
    sp->rule = 0;
    sp->fallback = 0;
    sp->prec = -1;
    sp->assoc = UNK;
    sp->firstset = 0;
    sp->lambda = LEMON_FALSE;
    sp->destructor = 0;
    sp->destLineno = 0;
    sp->datatype = 0;
    sp->useCnt = 0;
    Symbol_insert(sp,sp->name);
  }
  sp->useCnt++;
  return sp;
}

/* Compare two symbols for sorting purposes.  Return negative,
** zero, or positive if a is less then, equal to, or greater
** than b.
**
** Symbols that begin with upper case letters (terminals or tokens)
** must sort before symbols that begin with lower case letters
** (non-terminals).  And MULTITERMINAL symbols (created using the
** %token_class directive) must sort at the very end. Other than
** that, the order does not matter.
**
** We find experimentally that leaving the symbols in their original
** order (the order they appeared in the grammar file) gives the
** smallest parser tables in SQLite.
*/
int Symbolcmpp(const void *_a, const void *_b)
{
  const struct symbol *a = *(const struct symbol **) _a;
  const struct symbol *b = *(const struct symbol **) _b;
  int i1 = a->type==MULTITERMINAL ? 3 : a->name[0]>'Z' ? 2 : 1;
  int i2 = b->type==MULTITERMINAL ? 3 : b->name[0]>'Z' ? 2 : 1;
  return i1==i2 ? a->index - b->index : i1 - i2;
}

/* There is one instance of the following structure for each
** associative array of type "x2".
*/
struct s_x2 {
  int size;               /* The number of available slots. */
                          /*   Must be a power of 2 greater than or */
                          /*   equal to 1 */
  int count;              /* Number of currently slots filled */
  struct s_x2node *tbl;  /* The data stored here */
  struct s_x2node **ht;  /* Hash table for lookups */
};

/* There is one instance of this structure for every data element
** in an associative array of type "x2".
*/
typedef struct s_x2node {
  struct symbol *data;     /* The data */
  const char *key;         /* The key */
  struct s_x2node *next;   /* Next entry with the same hash */
  struct s_x2node **from;  /* Previous link */
} x2node;

/* There is only one instance of the array, which is the following */
static struct s_x2 *x2a;

/* Allocate a new associative array */
void Symbol_init(void){
  if( x2a ) return;
  x2a = (struct s_x2*)lime_malloc( sizeof(struct s_x2) );
  if( x2a ){
    x2a->size = 128;
    x2a->count = 0;
    x2a->tbl = (x2node*)lime_calloc(128, sizeof(x2node) + sizeof(x2node*));
    if( x2a->tbl==0 ){
      lime_free(x2a);
      x2a = 0;
    }else{
      int i;
      x2a->ht = (x2node**)&(x2a->tbl[128]);
      for(i=0; i<128; i++) x2a->ht[i] = 0;
    }
  }
}
/* Insert a new record into the array.  Return TRUE if successful.
** Prior data with the same key is NOT overwritten */
int Symbol_insert(struct symbol *data, const char *key)
{
  x2node *np;
  unsigned h;
  unsigned ph;

  if( x2a==0 ) return 0;
  ph = strhash(key);
  h = ph & (x2a->size-1);
  np = x2a->ht[h];
  while( np ){
    if( strcmp(np->key,key)==0 ){
      /* An existing entry with the same key is found. */
      /* Fail because overwrite is not allows. */
      return 0;
    }
    np = np->next;
  }
  if( x2a->count>=x2a->size ){
    /* Need to make the hash table bigger */
    int i,arrSize;
    struct s_x2 array;
    array.size = arrSize = x2a->size*2;
    array.count = x2a->count;
    array.tbl = (x2node*)lime_calloc(arrSize, sizeof(x2node)+sizeof(x2node*));
    if( array.tbl==0 ) return 0;  /* Fail due to malloc failure */
    array.ht = (x2node**)&(array.tbl[arrSize]);
    for(i=0; i<arrSize; i++) array.ht[i] = 0;
    for(i=0; i<x2a->count; i++){
      x2node *oldnp, *newnp;
      oldnp = &(x2a->tbl[i]);
      h = strhash(oldnp->key) & (arrSize-1);
      newnp = &(array.tbl[i]);
      if( array.ht[h] ) array.ht[h]->from = &(newnp->next);
      newnp->next = array.ht[h];
      newnp->key = oldnp->key;
      newnp->data = oldnp->data;
      newnp->from = &(array.ht[h]);
      array.ht[h] = newnp;
    }
    /* lime_free(x2a->tbl); // This program was originally written for 16-bit
    ** machines.  Don't worry about freeing this trivial amount of memory
    ** on modern platforms.  Just leak it. */
    *x2a = array;
  }
  /* Insert the new data */
  h = ph & (x2a->size-1);
  np = &(x2a->tbl[x2a->count++]);
  np->key = key;
  np->data = data;
  if( x2a->ht[h] ) x2a->ht[h]->from = &(np->next);
  np->next = x2a->ht[h];
  x2a->ht[h] = np;
  np->from = &(x2a->ht[h]);
  return 1;
}

/* Return a pointer to data assigned to the given key.  Return NULL
** if no such key. */
struct symbol *Symbol_find(const char *key)
{
  unsigned h;
  x2node *np;

  if( x2a==0 ) return 0;
  h = strhash(key) & (x2a->size-1);
  np = x2a->ht[h];
  while( np ){
    if( strcmp(np->key,key)==0 ) break;
    np = np->next;
  }
  return np ? np->data : 0;
}

/* Return the n-th data.  Return NULL if n is out of range. */
struct symbol *Symbol_Nth(int n)
{
  struct symbol *data;
  if( x2a && n>0 && n<=x2a->count ){
    data = x2a->tbl[n-1].data;
  }else{
    data = 0;
  }
  return data;
}

/* Return the size of the array */
int Symbol_count()
{
  return x2a ? x2a->count : 0;
}

/* Return an array of pointers to all data in the table.
** The array is obtained from malloc.  Return NULL if memory allocation
** problems, or if the array is empty. */
struct symbol **Symbol_arrayof()
{
  struct symbol **array;
  int i,arrSize;
  if( x2a==0 ) return 0;
  arrSize = x2a->count;
  array = (struct symbol **)lime_calloc(arrSize, sizeof(struct symbol *));
  if( array ){
    for(i=0; i<arrSize; i++) array[i] = x2a->tbl[i].data;
  }
  return array;
}

/* Compare two configurations */
int Configcmp(const char *_a,const char *_b)
{
  const struct config *a = (struct config *) _a;
  const struct config *b = (struct config *) _b;
  int x;
  x = a->rp->index - b->rp->index;
  if( x==0 ) x = a->dot - b->dot;
  return x;
}

/* Compare two states */
PRIVATE int statecmp(struct config *a, struct config *b)
{
  int rc;
  for(rc=0; rc==0 && a && b;  a=a->bp, b=b->bp){
    rc = a->rp->index - b->rp->index;
    if( rc==0 ) rc = a->dot - b->dot;
  }
  if( rc==0 ){
    if( a ) rc = 1;
    if( b ) rc = -1;
  }
  return rc;
}

/* Hash a state */
PRIVATE unsigned statehash(struct config *a)
{
  unsigned h=0;
  while( a ){
    h = h*571 + a->rp->index*37 + a->dot;
    a = a->bp;
  }
  return h;
}

/* Allocate a new state structure */
struct state *State_new()
{
  struct state *newstate;
  newstate = (struct state *)lime_calloc(1, sizeof(struct state) );
  MemoryCheck(newstate);
  return newstate;
}

/* There is one instance of the following structure for each
** associative array of type "x3".
*/
struct s_x3 {
  int size;               /* The number of available slots. */
                          /*   Must be a power of 2 greater than or */
                          /*   equal to 1 */
  int count;              /* Number of currently slots filled */
  struct s_x3node *tbl;  /* The data stored here */
  struct s_x3node **ht;  /* Hash table for lookups */
};

/* There is one instance of this structure for every data element
** in an associative array of type "x3".
*/
typedef struct s_x3node {
  struct state *data;                  /* The data */
  struct config *key;                   /* The key */
  struct s_x3node *next;   /* Next entry with the same hash */
  struct s_x3node **from;  /* Previous link */
} x3node;

/* There is only one instance of the array, which is the following */
static struct s_x3 *x3a;

/* Allocate a new associative array */
void State_init(void){
  if( x3a ) return;
  x3a = (struct s_x3*)lime_malloc( sizeof(struct s_x3) );
  if( x3a ){
    x3a->size = 128;
    x3a->count = 0;
    x3a->tbl = (x3node*)lime_calloc(128, sizeof(x3node) + sizeof(x3node*));
    if( x3a->tbl==0 ){
      lime_free(x3a);
      x3a = 0;
    }else{
      int i;
      x3a->ht = (x3node**)&(x3a->tbl[128]);
      for(i=0; i<128; i++) x3a->ht[i] = 0;
    }
  }
}
/* Insert a new record into the array.  Return TRUE if successful.
** Prior data with the same key is NOT overwritten */
int State_insert(struct state *data, struct config *key)
{
  x3node *np;
  unsigned h;
  unsigned ph;

  if( x3a==0 ) return 0;
  ph = statehash(key);
  h = ph & (x3a->size-1);
  np = x3a->ht[h];
  while( np ){
    if( statecmp(np->key,key)==0 ){
      /* An existing entry with the same key is found. */
      /* Fail because overwrite is not allows. */
      return 0;
    }
    np = np->next;
  }
  if( x3a->count>=x3a->size ){
    /* Need to make the hash table bigger */
    int i,arrSize;
    struct s_x3 array;
    array.size = arrSize = x3a->size*2;
    array.count = x3a->count;
    array.tbl = (x3node*)lime_calloc(arrSize, sizeof(x3node)+sizeof(x3node*));
    if( array.tbl==0 ) return 0;  /* Fail due to malloc failure */
    array.ht = (x3node**)&(array.tbl[arrSize]);
    for(i=0; i<arrSize; i++) array.ht[i] = 0;
    for(i=0; i<x3a->count; i++){
      x3node *oldnp, *newnp;
      oldnp = &(x3a->tbl[i]);
      h = statehash(oldnp->key) & (arrSize-1);
      newnp = &(array.tbl[i]);
      if( array.ht[h] ) array.ht[h]->from = &(newnp->next);
      newnp->next = array.ht[h];
      newnp->key = oldnp->key;
      newnp->data = oldnp->data;
      newnp->from = &(array.ht[h]);
      array.ht[h] = newnp;
    }
    lime_free(x3a->tbl);
    *x3a = array;
  }
  /* Insert the new data */
  h = ph & (x3a->size-1);
  np = &(x3a->tbl[x3a->count++]);
  np->key = key;
  np->data = data;
  if( x3a->ht[h] ) x3a->ht[h]->from = &(np->next);
  np->next = x3a->ht[h];
  x3a->ht[h] = np;
  np->from = &(x3a->ht[h]);
  return 1;
}

/* Return a pointer to data assigned to the given key.  Return NULL
** if no such key. */
struct state *State_find(struct config *key)
{
  unsigned h;
  x3node *np;

  if( x3a==0 ) return 0;
  h = statehash(key) & (x3a->size-1);
  np = x3a->ht[h];
  while( np ){
    if( statecmp(np->key,key)==0 ) break;
    np = np->next;
  }
  return np ? np->data : 0;
}

/* Return an array of pointers to all data in the table.
** The array is obtained from malloc.  Return NULL if memory allocation
** problems, or if the array is empty. */
struct state **State_arrayof(void)
{
  struct state **array;
  int i,arrSize;
  if( x3a==0 ) return 0;
  arrSize = x3a->count;
  array = (struct state **)lime_calloc(arrSize, sizeof(struct state *));
  if( array ){
    for(i=0; i<arrSize; i++) array[i] = x3a->tbl[i].data;
  }
  return array;
}

/* Hash a configuration */
PRIVATE unsigned confighash(struct config *a)
{
  unsigned h=0;
  h = a->rp->index*37 + a->dot;
  return h;
}

/* There is one instance of the following structure for each
** associative array of type "x4".
*/
struct s_x4 {
  int size;               /* The number of available slots. */
                          /*   Must be a power of 2 greater than or */
                          /*   equal to 1 */
  int count;              /* Number of currently slots filled */
  struct s_x4node *tbl;  /* The data stored here */
  struct s_x4node **ht;  /* Hash table for lookups */
};

/* There is one instance of this structure for every data element
** in an associative array of type "x4".
*/
typedef struct s_x4node {
  struct config *data;                  /* The data */
  struct s_x4node *next;   /* Next entry with the same hash */
  struct s_x4node **from;  /* Previous link */
} x4node;

/* There is only one instance of the array, which is the following */
static struct s_x4 *x4a;

/* Allocate a new associative array */
void Configtable_init(void){
  if( x4a ) return;
  x4a = (struct s_x4*)lime_malloc( sizeof(struct s_x4) );
  if( x4a ){
    x4a->size = 64;
    x4a->count = 0;
    x4a->tbl = (x4node*)lime_calloc(64, sizeof(x4node) + sizeof(x4node*));
    if( x4a->tbl==0 ){
      lime_free(x4a);
      x4a = 0;
    }else{
      int i;
      x4a->ht = (x4node**)&(x4a->tbl[64]);
      for(i=0; i<64; i++) x4a->ht[i] = 0;
    }
  }
}
/* Insert a new record into the array.  Return TRUE if successful.
** Prior data with the same key is NOT overwritten */
int Configtable_insert(struct config *data)
{
  x4node *np;
  unsigned h;
  unsigned ph;

  if( x4a==0 ) return 0;
  ph = confighash(data);
  h = ph & (x4a->size-1);
  np = x4a->ht[h];
  while( np ){
    if( Configcmp((const char *) np->data,(const char *) data)==0 ){
      /* An existing entry with the same key is found. */
      /* Fail because overwrite is not allows. */
      return 0;
    }
    np = np->next;
  }
  if( x4a->count>=x4a->size ){
    /* Need to make the hash table bigger */
    int i,arrSize;
    struct s_x4 array;
    array.size = arrSize = x4a->size*2;
    array.count = x4a->count;
    array.tbl = (x4node*)lime_calloc(arrSize,
                                      sizeof(x4node) + sizeof(x4node*));
    if( array.tbl==0 ) return 0;  /* Fail due to malloc failure */
    array.ht = (x4node**)&(array.tbl[arrSize]);
    for(i=0; i<arrSize; i++) array.ht[i] = 0;
    for(i=0; i<x4a->count; i++){
      x4node *oldnp, *newnp;
      oldnp = &(x4a->tbl[i]);
      h = confighash(oldnp->data) & (arrSize-1);
      newnp = &(array.tbl[i]);
      if( array.ht[h] ) array.ht[h]->from = &(newnp->next);
      newnp->next = array.ht[h];
      newnp->data = oldnp->data;
      newnp->from = &(array.ht[h]);
      array.ht[h] = newnp;
    }
    *x4a = array;
  }
  /* Insert the new data */
  h = ph & (x4a->size-1);
  np = &(x4a->tbl[x4a->count++]);
  np->data = data;
  if( x4a->ht[h] ) x4a->ht[h]->from = &(np->next);
  np->next = x4a->ht[h];
  x4a->ht[h] = np;
  np->from = &(x4a->ht[h]);
  return 1;
}

/* Return a pointer to data assigned to the given key.  Return NULL
** if no such key. */
struct config *Configtable_find(struct config *key)
{
  int h;
  x4node *np;

  if( x4a==0 ) return 0;
  h = confighash(key) & (x4a->size-1);
  np = x4a->ht[h];
  while( np ){
    if( Configcmp((const char *) np->data,(const char *) key)==0 ) break;
    np = np->next;
  }
  return np ? np->data : 0;
}

/* Remove all data from the table.  Pass each data to the function "f"
** as it is removed.  ("f" may be null to avoid this step.) */
void Configtable_clear(int(*f)(struct config *))
{
  int i;
  if( x4a==0 || x4a->count==0 ) return;
  if( f ) for(i=0; i<x4a->count; i++) (*f)(x4a->tbl[i].data);
  for(i=0; i<x4a->size; i++) x4a->ht[i] = 0;
  x4a->count = 0;
  return;
}

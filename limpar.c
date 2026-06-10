/*
** 2000-05-29
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** Driver template for the lime parser generator.
**
** The "lemon" program processes an LALR(1) input grammar file, then uses
** this template to construct a parser.  The "lemon" program inserts text
** at each "%%" line.  Also, any "P-a-r-s-e" identifier prefix (without the
** interstitial "-" characters) contained in this template is changed into
** the value of the %name directive from the grammar.  Otherwise, the content
** of this template is copied straight through into the generated parser
** source file.
**
** The following is the concatenation of all %include directives from the
** input grammar file:
*/
/************ Begin %include sections from the grammar ************************/
%%
/**************** End of %include directives **********************************/
/* These constants specify the various numeric values for terminal symbols.
***************** Begin token definitions *************************************/
%%
/**************** End token definitions ***************************************/

/* The next sections is a series of control #defines.
** various aspects of the generated parser.
**    YYCODETYPE         is the data type used to store the integer codes
**                       that represent terminal and non-terminal symbols.
**                       "unsigned char" is used if there are fewer than
**                       256 symbols.  Larger types otherwise.
**    YYNOCODE           is a number of type YYCODETYPE that is not used for
**                       any terminal or nonterminal symbol.
**    YYFALLBACK         If defined, this indicates that one or more tokens
**                       (also known as: "terminal symbols") have fall-back
**                       values which should be used if the original symbol
**                       would not parse.  This permits keywords to sometimes
**                       be used as identifiers, for example.
**    YYACTIONTYPE       is the data type used for "action codes" - numbers
**                       that indicate what to do in response to the next
**                       token.
**    ParseTOKENTYPE     is the data type used for minor type for terminal
**                       symbols.  Background: A "minor type" is a semantic
**                       value associated with a terminal or non-terminal
**                       symbols.  For example, for an "ID" terminal symbol,
**                       the minor type might be the name of the identifier.
**                       Each non-terminal can have a different minor type.
**                       Terminal symbols all have the same minor type, though.
**                       This macros defines the minor type for terminal 
**                       symbols.
**    YYMINORTYPE        is the data type used for all minor types.
**                       This is typically a union of many types, one of
**                       which is ParseTOKENTYPE.  The entry in the union
**                       for terminal symbols is called "yy0".
**    YYSTACKDEPTH       is the maximum depth of the parser's stack.  If
**                       zero the stack is dynamically sized using realloc()
**    ParseARG_SDECL     A static variable declaration for the %extra_argument
**    ParseARG_PDECL     A parameter declaration for the %extra_argument
**    ParseARG_PARAM     Code to pass %extra_argument as a subroutine parameter
**    ParseARG_STORE     Code to store %extra_argument into yypParser
**    ParseARG_FETCH     Code to extract %extra_argument from yypParser
**    ParseCTX_*         As ParseARG_ except for %extra_context
**    YYREALLOC          Name of the realloc() function to use
**    YYFREE             Name of the free() function to use
**    YYDYNSTACK         True if stack space should be extended on heap
**    YYERRORSYMBOL      is the code number of the error symbol.  If not
**                       defined, then do no error processing.
**    YYNSTATE           the combined number of states.
**    YYNRULE            the number of rules in the grammar
**    YYNTOKEN           Number of terminal symbols
**    YY_MAX_SHIFT       Maximum value for shift actions
**    YY_MIN_SHIFTREDUCE Minimum value for shift-reduce actions
**    YY_MAX_SHIFTREDUCE Maximum value for shift-reduce actions
**    YY_ERROR_ACTION    The yy_action[] code for syntax error
**    YY_ACCEPT_ACTION   The yy_action[] code for accept
**    YY_NO_ACTION       The yy_action[] code for no-op
**    YY_MIN_REDUCE      Minimum value for reduce actions
**    YY_MAX_REDUCE      Maximum value for reduce actions
**    YY_MIN_DSTRCTR     Minimum symbol value that has a destructor
**    YY_MAX_DSTRCTR     Maximum symbol value that has a destructor
*/
#ifndef INTERFACE
# define INTERFACE 1
#endif
/************* Begin control #defines *****************************************/
%%
/************* End control #defines *******************************************/
#define YY_NLOOKAHEAD ((int)(sizeof(yy_lookahead)/sizeof(yy_lookahead[0])))

/* Define the yytestcase() macro to be a no-op if is not already defined
** otherwise.
**
** Applications can choose to define yytestcase() in the %include section
** to a macro that can assist in verifying code coverage.  For production
** code the yytestcase() macro should be turned off.  But it is useful
** for testing.
*/
#ifndef yytestcase
# define yytestcase(X)
#endif

/* Branch-prediction hints.  These are no-ops on compilers without
** __builtin_expect support; the runtime is correct either way.
** Used in the hot find_shift / find_reduce / dispatch paths to
** mark statistically-rare branches (collisions, accept, error).
** Per .agent/notes/c-perf-audit.md item #5: limpar.c was the
** runtime template every user binary embeds, and had zero hints. */
#ifndef LIME_LIKELY
# if defined(__GNUC__) || defined(__clang__)
#  define LIME_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define LIME_UNLIKELY(x) __builtin_expect(!!(x), 0)
# else
#  define LIME_LIKELY(x)   (x)
#  define LIME_UNLIKELY(x) (x)
# endif
#endif

/* Macro to determine if stack space has the ability to grow using
** heap memory.
*/
#if YYSTACKDEPTH<=0 || YYDYNSTACK
# define YYGROWABLESTACK 1
#else
# define YYGROWABLESTACK 0
#endif

/* Guarantee a minimum number of initial stack slots.
*/
#if YYSTACKDEPTH<=0
# undef YYSTACKDEPTH
# define YYSTACKDEPTH 2  /* Need a minimum stack size */
#endif


/* Next are the tables used to determine what action to take based on the
** current state and lookahead token.  These tables are used to implement
** functions that take a state number and lookahead value and return an
** action integer.  
**
** Suppose the action integer is N.  Then the action is determined as
** follows
**
**   0 <= N <= YY_MAX_SHIFT             Shift N.  That is, push the lookahead
**                                      token onto the stack and goto state N.
**
**   N between YY_MIN_SHIFTREDUCE       Shift to an arbitrary state then
**     and YY_MAX_SHIFTREDUCE           reduce by rule N-YY_MIN_SHIFTREDUCE.
**
**   N == YY_ERROR_ACTION               A syntax error has occurred.
**
**   N == YY_ACCEPT_ACTION              The parser accepts its input.
**
**   N == YY_NO_ACTION                  No such action.  Denotes unused
**                                      slots in the yy_action[] table.
**
**   N between YY_MIN_REDUCE            Reduce by rule N-YY_MIN_REDUCE
**     and YY_MAX_REDUCE
**
** The action table is constructed as a single large table named yy_action[].
** Given state S and lookahead X, the action is computed as either:
**
**    (A)   N = yy_action[ yy_shift_ofst[S] + X ]
**    (B)   N = yy_default[S]
**
** The (A) formula is preferred.  The B formula is used instead if
** yy_lookahead[yy_shift_ofst[S]+X] is not equal to X.
**
** The formulas above are for computing the action when the lookahead is
** a terminal symbol.  If the lookahead is a non-terminal (as occurs after
** a reduce action) then the yy_reduce_ofst[] array is used in place of
** the yy_shift_ofst[] array.
**
** The following are the tables generated in this section:
**
**  yy_action[]        A single table containing all actions.
**  yy_lookahead[]     A table containing the lookahead for each entry in
**                     yy_action.  Used to detect hash collisions.
**  yy_shift_ofst[]    For each state, the offset into yy_action for
**                     shifting terminals.
**  yy_reduce_ofst[]   For each state, the offset into yy_action for
**                     shifting non-terminals after a reduce.
**  yy_default[]       Default action for each state.
**
*********** Begin parsing tables **********************************************/
%%
/********** End of lemon-generated parsing tables *****************************/

/* The next table maps tokens (terminal symbols) into fallback tokens.  
** If a construct like the following:
** 
**      %fallback ID X Y Z.
**
** appears in the grammar, then ID becomes a fallback token for X, Y,
** and Z.  Whenever one of the tokens X, Y, or Z is input to the parser
** but it does not parse, the type of the token is changed to ID and
** the parse is retried before an error is thrown.
**
** This feature can be used, for example, to cause some keywords in a language
** to revert to identifiers if they keyword does not apply in the context where
** it appears.
*/
#ifdef YYFALLBACK
static const YYCODETYPE yyFallback[] = {
%%
};
#endif /* YYFALLBACK */

/* The following structure represents a single element of the
** parser's stack.  Information stored includes:
**
**   +  The state number for the parser at this level of the stack.
**
**   +  The value of the token stored at this level of the stack.
**      (In other words, the "major" token.)
**
**   +  The semantic value stored at this level of the stack.  This is
**      the information used by the action routines in the grammar.
**      It is sometimes called the "minor" token.
**
** After the "shift" half of a SHIFTREDUCE action, the stateno field
** actually contains the reduce action for the second half of the
** SHIFTREDUCE.
*/
struct yyStackEntry {
  YYACTIONTYPE stateno;  /* The state-number, or reduce action in SHIFTREDUCE */
  YYCODETYPE major;      /* The major token value.  This is the code
                         ** number for the token at this stack level */
  YYMINORTYPE minor;     /* The user-supplied minor token value.  This
                         ** is the value of the token  */
#ifdef YYLOCATIONTYPE
  YYLOCATIONTYPE yyloc;  /* Source location for this stack entry.
                         ** Only present when %locations is active. */
#endif
};
typedef struct yyStackEntry yyStackEntry;

/* The state of the parser is completely contained in an instance of
** the following structure */
struct yyParser {
  yyStackEntry *yytos;          /* Pointer to top element of the stack */
#ifdef YYTRACKMAXSTACKDEPTH
  int yyhwm;                    /* High-water mark of the stack */
#endif
#ifndef YYNOERRORRECOVERY
  int yyerrcnt;                 /* Shifts left before out of the error */
#endif
#ifdef YYLOCATIONTYPE
  /* Source location of the token currently being processed.  Updated
  ** at each ParseLoc() entry so that %syntax_error sees the location
  ** of the offending lookahead, not the location of the stack-top
  ** symbol (which would be the previously-shifted token, off by one). */
  YYLOCATIONTYPE yyLookaheadLoc;
#endif
  /* Action-time lookahead access (P0-NEW-5).  While Parse() runs a
  ** sequence of reduces before shifting the just-pushed token, the
  ** token's external code and value are stashed here so that user
  ** action code can read them via Parse_get_lookahead() and
  ** optionally signal consumption via Parse_clear_lookahead().  This
  ** is the equivalent of bison's yychar/yyclearin and exists to
  ** support grammars (notably PostgreSQL's plpgsql pl_gram.y) where
  ** an empty production needs to consume the lookahead and reach
  ** into the lexer for further tokens.
  **
  ** yyLookaheadMajor is YYEMPTY (-2) when no Parse() call is in
  ** progress.  yyLookaheadCleared is set by Parse_clear_lookahead()
  ** to tell the dispatch loop to skip the trailing shift and return,
  ** treating the lookahead as consumed by the action body. */
  int yyLookaheadMajor;
  ParseTOKENTYPE yyLookaheadMinor;
  char yyLookaheadCleared;
  ParseARG_SDECL                /* A place to hold %extra_argument */
  ParseCTX_SDECL                /* A place to hold %extra_context */
  yyStackEntry *yystackEnd;           /* Last entry in the stack */
  yyStackEntry *yystack;              /* The parser stack */
  yyStackEntry yystk0[YYSTACKDEPTH];  /* Initial stack space */
};
typedef struct yyParser yyParser;

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#ifndef NDEBUG
#include <stdio.h>
static FILE *yyTraceFILE = 0;
static char *yyTracePrompt = 0;
#endif /* NDEBUG */

#ifndef NDEBUG
/* 
** Turn parser tracing on by giving a stream to which to write the trace
** and a prompt to preface each trace message.  Tracing is turned off
** by making either argument NULL 
**
** Inputs:
** <ul>
** <li> A FILE* to which trace output should be written.
**      If NULL, then tracing is turned off.
** <li> A prefix string written at the beginning of every
**      line of trace output.  If NULL, then tracing is
**      turned off.
** </ul>
**
** Outputs:
** None.
*/
void ParseTrace(FILE *TraceFILE, char *zTracePrompt){
  yyTraceFILE = TraceFILE;
  yyTracePrompt = zTracePrompt;
  if( yyTraceFILE==0 ) yyTracePrompt = 0;
  else if( yyTracePrompt==0 ) yyTraceFILE = 0;
}
#endif /* NDEBUG */

/* Terminal and nonterminal names.  Always emitted (required by
** ParseTokenName, ParseExpectedTokens, ParseExpectedTokensString,
** and optionally by trace/coverage code). */
static const char *const yyTokenName[] = { 
%%
};

#ifndef NDEBUG
/* For tracing reduce actions, the names of all rules are required.
*/
static const char *const yyRuleName[] = {
%%
};
#endif /* NDEBUG */


#if YYGROWABLESTACK
/*
** Try to increase the size of the parser stack.  Return the number
** of errors.  Return 0 on success.
*/
static int yyGrowStack(yyParser *p){
  int oldSize = 1 + (int)(p->yystackEnd - p->yystack);
  int newSize;
  int idx;
  yyStackEntry *pNew;
#ifdef YYSIZELIMIT
  int nLimit = YYSIZELIMIT(ParseCTX(p));
#endif

  newSize = oldSize*2 + 100;
#ifdef YYSIZELIMIT
  if( newSize>nLimit ){
    newSize = nLimit;
    if( newSize<=oldSize ) return 1;
  }
#endif
  idx = (int)(p->yytos - p->yystack);
  if( p->yystack==p->yystk0 ){
    pNew = YYREALLOC(0, newSize*sizeof(pNew[0]), ParseCTX(p));
    if( pNew==0 ) return 1;
    memcpy(pNew, p->yystack, oldSize*sizeof(pNew[0]));
  }else{
    pNew = YYREALLOC(p->yystack, newSize*sizeof(pNew[0]), ParseCTX(p));
    if( pNew==0 ) return 1;
  }
  p->yystack = pNew;
  p->yytos = &p->yystack[idx];
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sStack grows from %d to %d entries.\n",
            yyTracePrompt, oldSize, newSize);
  }
#endif
  p->yystackEnd = &p->yystack[newSize-1];
  return 0;
}
#endif /* YYGROWABLESTACK */

#if !YYGROWABLESTACK
/* For builds that do no have a growable stack, yyGrowStack always
** returns an error.
*/
# define yyGrowStack(X) 1
#endif

/* Datatype of the argument to the memory allocated passed as the
** second argument to ParseAlloc() below.  This can be changed by
** putting an appropriate #define in the %include section of the input
** grammar.
*/
#ifndef YYMALLOCARGTYPE
# define YYMALLOCARGTYPE size_t
#endif

/* Initialize a new parser that has already been allocated.
*/
void ParseInit(void *yypRawParser ParseCTX_PDECL){
  yyParser *yypParser = (yyParser*)yypRawParser;
  ParseCTX_STORE
#ifdef YYTRACKMAXSTACKDEPTH
  yypParser->yyhwm = 0;
#endif
  yypParser->yystack = yypParser->yystk0;
  yypParser->yystackEnd = &yypParser->yystack[YYSTACKDEPTH-1];
#ifndef YYNOERRORRECOVERY
  yypParser->yyerrcnt = -1;
#endif
#ifdef YYLOCATIONTYPE
  /* Zero-initialise the lookahead-location stash so %syntax_error
  ** sees a defined value if it fires before the first ParseLoc() call,
  ** or in pull-parser mode (Parse() without ParseLoc()). */
  memset(&yypParser->yyLookaheadLoc, 0, sizeof(yypParser->yyLookaheadLoc));
  /* P0-NEW-6: zero-init the sentinel slot's yyloc.  When yy_reduce
  ** invokes a user-defined YYLLOC_DEFAULT it reads yymsp[-1].yyloc
  ** (the location of the slot below the rule, Bison's Rhs[0]).  For
  ** the very first reduce -- particularly an empty rule -- yymsp[-1]
  ** is yystack[0], the parser's stack sentinel.  Without this init,
  ** that read returns whatever was in heap memory at allocation. */
  memset(&yypParser->yystack[0].yyloc, 0, sizeof(yypParser->yystack[0].yyloc));
#endif
  /* Initialise action-time lookahead state (P0-NEW-5).  YYEMPTY is
  ** -2 by convention (matching bison's yychar empty value); use it
  ** as a sentinel for "no Parse() call in progress". */
  yypParser->yyLookaheadMajor = -2;
  memset(&yypParser->yyLookaheadMinor, 0, sizeof(yypParser->yyLookaheadMinor));
  yypParser->yyLookaheadCleared = 0;
  yypParser->yytos = yypParser->yystack;
  yypParser->yystack[0].stateno = 0;
  yypParser->yystack[0].major = 0;
}

#ifndef Parse_ENGINEALWAYSONSTACK
/* 
** This function allocates a new parser.
** The only argument is a pointer to a function which works like
** malloc.
**
** Inputs:
** A pointer to the function used to allocate memory.
**
** Outputs:
** A pointer to a parser.  This pointer is used in subsequent calls
** to Parse and ParseFree.
*/
void *ParseAlloc(void *(*mallocProc)(YYMALLOCARGTYPE) ParseCTX_PDECL){
  yyParser *yypParser;
  yypParser = (yyParser*)(*mallocProc)( (YYMALLOCARGTYPE)sizeof(yyParser) );
  if( yypParser ){
    ParseCTX_STORE
    ParseInit(yypParser ParseCTX_PARAM);
  }
  return (void*)yypParser;
}
#endif /* Parse_ENGINEALWAYSONSTACK */


/* The following function deletes the "minor type" or semantic value
** associated with a symbol.  The symbol can be either a terminal
** or nonterminal. "yymajor" is the symbol code, and "yypminor" is
** a pointer to the value to be deleted.  The code used to do the 
** deletions is derived from the %destructor and/or %token_destructor
** directives of the input grammar.
*/
static void yy_destructor(
  yyParser *yypParser,    /* The parser */
  YYCODETYPE yymajor,     /* Type code for object to destroy */
  YYMINORTYPE *yypminor   /* The object to be destroyed */
){
  ParseARG_FETCH
  ParseCTX_FETCH
  /* Suppress unused-variable warnings when no %destructor is defined */
  ParseARG_STORE
  ParseCTX_STORE
  (void)yypminor;
  switch( yymajor ){
    /* Here is inserted the actions which take place when a
    ** terminal or non-terminal is destroyed.  This can happen
    ** when the symbol is popped from the stack during a
    ** reduce or during error processing or when a parser is 
    ** being destroyed before it is finished parsing.
    **
    ** Note: during a reduce, the only symbols destroyed are those
    ** which appear on the RHS of the rule, but which are *not* used
    ** inside the C code.
    */
/********* Begin destructor definitions ***************************************/
%%
/********* End destructor definitions *****************************************/
    default:  break;   /* If no destructor action specified: do nothing */
  }
}

/*
** Pop the parser's stack once.
**
** If there is a destructor routine associated with the token which
** is popped from the stack, then call it.
*/
static void yy_pop_parser_stack(yyParser *pParser){
  yyStackEntry *yytos;
  assert( pParser->yytos!=0 );
  assert( pParser->yytos > pParser->yystack );
  yytos = pParser->yytos--;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sPopping %s\n",
      yyTracePrompt,
      yyTokenName[yytos->major]);
  }
#endif
  yy_destructor(pParser, yytos->major, &yytos->minor);
}

/*
** Clear all secondary memory allocations from the parser
*/
void ParseFinalize(void *p){
  yyParser *pParser = (yyParser*)p;

  /* In-lined version of calling yy_pop_parser_stack() for each
  ** element left in the stack */
  yyStackEntry *yytos = pParser->yytos;
  while( yytos>pParser->yystack ){
#ifndef NDEBUG
    if( yyTraceFILE ){
      fprintf(yyTraceFILE,"%sPopping %s\n",
        yyTracePrompt,
        yyTokenName[yytos->major]);
    }
#endif
    if( yytos->major>=YY_MIN_DSTRCTR ){
      yy_destructor(pParser, yytos->major, &yytos->minor);
    }
    yytos--;
  }

#if YYGROWABLESTACK
  if( pParser->yystack!=pParser->yystk0 ){
    YYFREE(pParser->yystack, ParseCTX(pParser));
  }
#endif
}

#ifndef Parse_ENGINEALWAYSONSTACK
/* 
** Deallocate and destroy a parser.  Destructors are called for
** all stack elements before shutting the parser down.
**
** If the YYPARSEFREENEVERNULL macro exists (for example because it
** is defined in a %include section of the input grammar) then it is
** assumed that the input pointer is never NULL.
*/
void ParseFree(
  void *p,                    /* The parser to be deleted */
  void (*freeProc)(void*)     /* Function used to reclaim memory */
){
#ifndef YYPARSEFREENEVERNULL
  if( p==0 ) return;
#endif
  ParseFinalize(p);
  (*freeProc)(p);
}
#endif /* Parse_ENGINEALWAYSONSTACK */

/**
 * Reset a parser to its initial state without freeing the parser
 * struct or stack memory.  Pops everything off the stack
 * (running %destructor), resets state-0 markers + error count
 * + high-water mark.  user-arg is preserved.  Available v0.6.1.
 */
void ParseReset(void *yyp){
  yyParser *pParser = (yyParser*)yyp;
#ifndef YYPARSEFREENEVERNULL
  if( pParser==0 ) return;
#endif
  /* Pop everything off the stack, running destructors */
  while( pParser->yytos > pParser->yystack ){
#ifndef NDEBUG
    if( yyTraceFILE ){
      fprintf(yyTraceFILE,"%sPopping %s\n",
        yyTracePrompt,
        yyTokenName[pParser->yytos->major]);
    }
#endif
    if( pParser->yytos->major >= YY_MIN_DSTRCTR ){
      yy_destructor(pParser, pParser->yytos->major, &pParser->yytos->minor);
    }
    pParser->yytos--;
  }
  /* Reset to initial state (preserve yystack, yystackEnd, and ARG/CTX) */
  pParser->yytos = pParser->yystack;
  pParser->yystack[0].stateno = 0;
  pParser->yystack[0].major = 0;
#ifdef YYTRACKMAXSTACKDEPTH
  pParser->yyhwm = 0;
#endif
#ifndef YYNOERRORRECOVERY
  pParser->yyerrcnt = -1;
#endif
#ifdef YYLOCATIONTYPE
  /* Zero-init lookahead and sentinel location */
  memset(&pParser->yyLookaheadLoc, 0, sizeof(pParser->yyLookaheadLoc));
  memset(&pParser->yystack[0].yyloc, 0, sizeof(pParser->yystack[0].yyloc));
#endif
  /* Reset action-time lookahead state */
  pParser->yyLookaheadMajor = -2;
  memset(&pParser->yyLookaheadMinor, 0, sizeof(pParser->yyLookaheadMinor));
  pParser->yyLookaheadCleared = 0;
}

/*
** Return the peak depth of the stack for a parser.
*/
#ifdef YYTRACKMAXSTACKDEPTH
int ParseStackPeak(void *p){
  yyParser *pParser = (yyParser*)p;
  return pParser->yyhwm;
}
#endif

/* This array of booleans keeps track of the parser statement
** coverage.  The element yycoverage[X][Y] is set when the parser
** is in state X and has a lookahead token Y.  In a well-tested
** systems, every element of this matrix should end up being set.
*/
#if defined(YYCOVERAGE)
static unsigned char yycoverage[YYNSTATE][YYNTOKEN];
#endif

/*
** Write into out a description of every state/lookahead combination that
**
**   (1)  has not been used by the parser, and
**   (2)  is not a syntax error.
**
** Return the number of missed state/lookahead combinations.
*/
#if defined(YYCOVERAGE)
int ParseCoverage(FILE *out){
  int stateno, iLookAhead, i;
  int nMissed = 0;
  for(stateno=0; stateno<YYNSTATE; stateno++){
    i = yy_shift_ofst[stateno];
    for(iLookAhead=0; iLookAhead<YYNTOKEN; iLookAhead++){
      if( yy_lookahead[i+iLookAhead]!=iLookAhead ) continue;
      if( yycoverage[stateno][iLookAhead]==0 ) nMissed++;
      if( out ){
        fprintf(out,"State %d lookahead %s %s\n", stateno,
                yyTokenName[iLookAhead],
                yycoverage[stateno][iLookAhead] ? "ok" : "missed");
      }
    }
  }
  return nMissed;
}
#endif

/*
** Find the appropriate action for a parser given the terminal
** look-ahead token iLookAhead.
*/
/* Forward declaration for AOT-compiled action lookup */
#ifdef YYAOT
extern YYACTIONTYPE yy_find_shift_action_aot(YYACTIONTYPE stateno,
                                              unsigned short iLookAhead);
#endif

static YYACTIONTYPE yy_find_shift_action(
  YYCODETYPE iLookAhead,    /* The look-ahead token */
  YYACTIONTYPE stateno      /* Current state number */
){
  int i;

  if( LIME_UNLIKELY(stateno>YY_MAX_SHIFT) ) return stateno;
#ifdef YYAOT
  /* Use the AOT-compiled switch-based lookup instead of table-driven */
  return yy_find_shift_action_aot(stateno, (unsigned short)iLookAhead);
#endif
  assert( stateno <= YY_SHIFT_COUNT );
#if defined(YYCOVERAGE)
  yycoverage[stateno][iLookAhead] = 1;
#endif
  do{
    i = yy_shift_ofst[stateno];
    assert( i>=0 );
    assert( i<=YY_ACTTAB_COUNT );
    assert( i+YYNTOKEN<=(int)YY_NLOOKAHEAD );
    assert( iLookAhead!=YYNOCODE );
    assert( iLookAhead < YYNTOKEN );
    i += iLookAhead;
    assert( i<(int)YY_NLOOKAHEAD );
    if( LIME_UNLIKELY(yy_lookahead[i]!=iLookAhead) ){
#ifdef YYFALLBACK
      YYCODETYPE iFallback;            /* Fallback token */
      assert( iLookAhead<sizeof(yyFallback)/sizeof(yyFallback[0]) );
      iFallback = yyFallback[iLookAhead];
      if( iFallback!=0 ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE, "%sFALLBACK %s => %s\n",
             yyTracePrompt, yyTokenName[iLookAhead], yyTokenName[iFallback]);
        }
#endif
        assert( yyFallback[iFallback]==0 ); /* Fallback loop must terminate */
        iLookAhead = iFallback;
        continue;
      }
#endif
#ifdef YYWILDCARD
      {
        int j = i - iLookAhead + YYWILDCARD;
        assert( j<(int)(sizeof(yy_lookahead)/sizeof(yy_lookahead[0])) );
        if( yy_lookahead[j]==YYWILDCARD && iLookAhead>0 ){
#ifndef NDEBUG
          if( yyTraceFILE ){
            fprintf(yyTraceFILE, "%sWILDCARD %s => %s\n",
               yyTracePrompt, yyTokenName[iLookAhead],
               yyTokenName[YYWILDCARD]);
          }
#endif /* NDEBUG */
          return yy_action[j];
        }
      }
#endif /* YYWILDCARD */
      return yy_default[stateno];
    }else{
      assert( i>=0 && i<(int)(sizeof(yy_action)/sizeof(yy_action[0])) );
      return yy_action[i];
    }
  }while(1);
}

/*
** Find the appropriate action for a parser given the non-terminal
** look-ahead token iLookAhead.
*/
static YYACTIONTYPE yy_find_reduce_action(
  YYACTIONTYPE stateno,     /* Current state number */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
#ifdef YYERRORSYMBOL
  if( stateno>YY_REDUCE_COUNT ){
    return yy_default[stateno];
  }
#else
  assert( stateno<=YY_REDUCE_COUNT );
#endif
  i = yy_reduce_ofst[stateno];
  assert( iLookAhead!=YYNOCODE );
  i += iLookAhead;
#ifdef YYERRORSYMBOL
  if( i<0 || i>=YY_ACTTAB_COUNT || yy_lookahead[i]!=iLookAhead ){
    return yy_default[stateno];
  }
#else
  assert( i>=0 && i<YY_ACTTAB_COUNT );
  assert( yy_lookahead[i]==iLookAhead );
#endif
  return yy_action[i];
}

/*
** The following routine is called if the stack overflows.
*/
static void yyStackOverflow(yyParser *yypParser){
   ParseARG_FETCH
   ParseCTX_FETCH
#ifndef NDEBUG
   if( yyTraceFILE ){
     fprintf(yyTraceFILE,"%sStack Overflow!\n",yyTracePrompt);
   }
#endif
   while( yypParser->yytos>yypParser->yystack ) yy_pop_parser_stack(yypParser);
   /* Here code is inserted which will execute if the parser
   ** stack every overflows */
/******** Begin %stack_overflow code ******************************************/
%%
/******** End %stack_overflow code ********************************************/
   ParseARG_STORE /* Suppress warning about unused %extra_argument var */
   ParseCTX_STORE
}

/*
** Print tracing information for a SHIFT action
*/
#ifndef NDEBUG
static void yyTraceShift(yyParser *yypParser, int yyNewState, const char *zTag){
  if( yyTraceFILE ){
    if( yyNewState<YYNSTATE ){
      fprintf(yyTraceFILE,"%s%s '%s', go to state %d\n",
         yyTracePrompt, zTag, yyTokenName[yypParser->yytos->major],
         yyNewState);
    }else{
      fprintf(yyTraceFILE,"%s%s '%s', pending reduce %d\n",
         yyTracePrompt, zTag, yyTokenName[yypParser->yytos->major],
         yyNewState - YY_MIN_REDUCE);
    }
  }
}
#else
# define yyTraceShift(X,Y,Z)
#endif

/*
** Perform a shift action.
*/
static void yy_shift(
  yyParser *yypParser,          /* The parser to be shifted */
  YYACTIONTYPE yyNewState,      /* The new state to shift in */
  YYCODETYPE yyMajor,           /* The major token to shift in */
  ParseTOKENTYPE yyMinor        /* The minor token to shift in */
){
  yyStackEntry *yytos;
  yypParser->yytos++;
#ifdef YYTRACKMAXSTACKDEPTH
  if( (int)(yypParser->yytos - yypParser->yystack)>yypParser->yyhwm ){
    yypParser->yyhwm++;
    assert( yypParser->yyhwm == (int)(yypParser->yytos - yypParser->yystack) );
  }
#endif
  yytos = yypParser->yytos;
  if( yytos>yypParser->yystackEnd ){
    if( yyGrowStack(yypParser) ){
      yypParser->yytos--;
      yyStackOverflow(yypParser);
      return;
    }
    yytos = yypParser->yytos;
    assert( yytos <= yypParser->yystackEnd );
  }
  if( yyNewState > YY_MAX_SHIFT ){
    yyNewState += YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE;
  }
  yytos->stateno = yyNewState;
  yytos->major = yyMajor;
  yytos->minor.yy0 = yyMinor;
  yyTraceShift(yypParser, yyNewState, "Shift");
}

/* For rule J, yyRuleInfoLhs[J] contains the symbol on the left-hand side
** of that rule */
static const YYCODETYPE yyRuleInfoLhs[] = {
%%
};

/* For rule J, yyRuleInfoNRhs[J] contains the negative of the number
** of symbols on the right-hand side of that rule. */
static const signed char yyRuleInfoNRhs[] = {
%%
};

static void yy_accept(yyParser*);  /* Forward Declaration */

/* Forward declarations for the action-time lookahead API
** (Parse_get_lookahead / Parse_clear_lookahead).  The definitions
** live just before void Parse() further down; these forward
** declarations let user action bodies inside yy_reduce reference
** the API without an out-of-order-symbol error.  P0-NEW-5. */
#ifndef YYEMPTY
# define YYEMPTY (-2)
#endif
int  Parse_get_lookahead(
  void *yyp,
  ParseTOKENTYPE *yyminor_out
#ifdef YYLOCATIONTYPE
  , YYLOCATIONTYPE *yyloc_out
#endif
);
void Parse_clear_lookahead(void *yyp);

/* P0-NEW-8: opt-in eager default-reduce drain.  After a Parse()
** call returns the parser may be in a state whose only available
** action is a reduce (independent of the next lookahead).  Bison's
** pull-mode fires those reduces eagerly between yylex calls; Lime's
** push-mode normally waits for the next push to confirm the
** reduction.  Parse_drain forces the eager-fire behavior on demand,
** letting drivers like ecpg's preproc -- whose action bodies have
** side effects (fprintf to base_yyout) that must precede the
** lexer's next echo of whitespace -- match Bison's timing.
**
** Loops while the top-of-stack state's default action is
** (a) a reduce and (b) unconditional (no token-specific entries
** would override it).  Stops on a shift state, an unconditional
** non-reduce default, or YY_ACCEPT.  Action bodies fired during
** drain see no lookahead (Parse_get_lookahead returns YYEMPTY).
** Safe to call between Parse() invocations; safe to call multiple
** times in a row (the second call is a no-op once the parser is
** quiescent). */
void Parse_drain(void *yyp ParseCTX_PDECL);

/*
** v0.6.0: per-rule reduce action callbacks.
**
** Each grammar rule's reduction code is emitted as its own
** static function `yy_rule_<N>(yy_reduce_ctx *ctx)` and
** dispatched through `yy_rule_reduce_fn[ruleno]` below.  This
** replaces Lemon's classical 30-year switch-on-yyruleno.
**
** Why the change:
**   1. JIT specialisation -- per-rule callbacks let the JIT
**      inline reduce code into the JIT-compiled trace,
**      eliminating the indirect call through yy_reduce.
**   2. Composition + extension -- merging two grammars now
**      concatenates `yy_rule_reduce_fn[]` arrays trivially
**      instead of doing source-level switch surgery.
**   3. Hot-rule annotation -- `__attribute__((hot))` on the
**      handful of rules a profile says are hottest is now
**      possible per-function (impossible per switch case).
**   4. Plugin overrides -- a future feature lets an extension
**      replace one rule's reduce action by overwriting one
**      slot in the dispatch array, no whole-grammar rebuild.
**
** ABI break vs v0.5.x: the generated .c file's reduce path
** changes shape but the runtime ABI (ParserSnapshot, the
** push-parser entry, the JIT) stays put.  See ROADMAP for
** the v0.6.0 charter.
*/
typedef struct yy_reduce_ctx {
  yyParser *yypParser;             /* The parser */
  yyStackEntry *yymsp;             /* Top of the parser's stack */
  int yyLookahead;                 /* Lookahead token or YYNOCODE */
  ParseTOKENTYPE yyLookaheadToken; /* Value of the lookahead token */
#ifdef YYLOCATIONTYPE
  YYLOCATIONTYPE *yyloc_lhs_ptr;   /* &yyloc_lhs in caller; per-rule
                                   ** functions read AND write through
                                   ** this pointer so @$ assignments
                                   ** propagate to the LHS slot. */
#endif
} yy_reduce_ctx;

/********** Begin per-rule reduce action functions ****************************/
%%
/********** End per-rule reduce action functions ******************************/

/*
** Perform a reduce action and the shift that must immediately
** follow the reduce.
**
** The yyLookahead and yyLookaheadToken parameters provide reduce actions
** access to the lookahead token (if any).  The yyLookahead will be YYNOCODE
** if the lookahead token has already been consumed.  As this procedure is
** only called from one place, optimizing compilers will in-line it, which
** means that the extra parameters have no performance impact.
*/
static YYACTIONTYPE yy_reduce(
  yyParser *yypParser,         /* The parser */
  unsigned int yyruleno,       /* Number of the rule by which to reduce */
  int yyLookahead,             /* Lookahead token, or YYNOCODE if none */
  ParseTOKENTYPE yyLookaheadToken  /* Value of the lookahead token */
  ParseCTX_PDECL                   /* %extra_context */
){
  int yygoto;                     /* The next state */
  YYACTIONTYPE yyact;             /* The next action */
  yyStackEntry *yymsp;            /* The top of the parser's stack */
  int yysize;                     /* Amount to pop the stack */
#ifdef YYLOCATIONTYPE
  /* P0-NEW-7: per-reduce LHS-location local.  Bound to @$ /
  ** @<lhsalias> in user action bodies, so the action sees
  ** Bison's documented pre-action default and may overwrite
  ** it.  Computed below before the action switch; committed
  ** to the LHS slot's yyloc after the stack adjustment. */
  YYLOCATIONTYPE yyloc_lhs;
# ifndef YYRHSLOC
#  define YYRHSLOC(Rhs, K) ((Rhs)[K])
# endif
#endif
  (void)yyLookahead;
  (void)yyLookaheadToken;
  yymsp = yypParser->yytos;

#ifdef YYLOCATIONTYPE
  /* Compute the LHS location BEFORE the action body, matching
  ** Bison's documented YYLLOC_DEFAULT ordering (P0-NEW-7).  The
  ** action body's @$ writes go to yyloc_lhs and are preserved;
  ** see P0-NEW-6 for why this matters (ecpg's preproc.y has 70+
  ** non-trivial @$ assignments that build source-text
  ** accumulation across each reduce). */
  {
    int yyN = -yyRuleInfoNRhs[yyruleno];   /* nrhs >= 0 */
# ifdef YYLLOC_DEFAULT
    /* User-defined override.  Build a 0-indexed YYLOCATIONTYPE
    ** array per Bison's signature: Rhs[i] for i in 1..N is the
    ** i-th RHS's location, Rhs[0] is the slot below the rule
    ** (used for empty-rule fallback). */
    YYLOCATIONTYPE yyloc_rhs[YYNRHS_MAX + 1];
    int yi;
    yyloc_rhs[0] = yymsp[-yyN].yyloc;
    for( yi=1; yi<=yyN; yi++ ){
      yyloc_rhs[yi] = yymsp[yi-yyN].yyloc;
    }
    YYLLOC_DEFAULT(yyloc_lhs, yyloc_rhs, yyN);
# else
    /* Built-in default: Rhs[1] for non-empty, lookahead for
    ** empty.  Same observable result as the prior post-action
    ** slot-reuse logic. */
    if( yyN == 0 ){
      yyloc_lhs = yypParser->yyLookaheadLoc;
    }else{
      yyloc_lhs = yymsp[1-yyN].yyloc;
    }
# endif
  }
#endif

  /* v0.6.0: dispatch the rule's reduction via the per-rule
  ** function pointer table.  The bookkeeping below (LHS lookup,
  ** stack pop+push, location commit) is unchanged. */
  assert( yyruleno<sizeof(yyRuleInfoLhs)/sizeof(yyRuleInfoLhs[0]) );
  assert( yyruleno<(sizeof(yy_rule_reduce_fn)/sizeof(yy_rule_reduce_fn[0])) );
  {
    yy_reduce_ctx yy_ctx;
    yy_ctx.yypParser = yypParser;
    yy_ctx.yymsp = yymsp;
    yy_ctx.yyLookahead = yyLookahead;
    yy_ctx.yyLookaheadToken = yyLookaheadToken;
#ifdef YYLOCATIONTYPE
    yy_ctx.yyloc_lhs_ptr = &yyloc_lhs;
#endif
    yy_rule_reduce_fn[yyruleno](&yy_ctx);
  }

  yygoto = yyRuleInfoLhs[yyruleno];
  yysize = yyRuleInfoNRhs[yyruleno];
  yyact = yy_find_reduce_action(yymsp[yysize].stateno,(YYCODETYPE)yygoto);

  /* There are no SHIFTREDUCE actions on nonterminals because the table
  ** generator has simplified them to pure REDUCE actions. */
  assert( !(yyact>YY_MAX_SHIFT && yyact<=YY_MAX_SHIFTREDUCE) );

  /* It is not possible for a REDUCE to be followed by an error */
  assert( yyact!=YY_ERROR_ACTION );

  yymsp += yysize+1;
  yypParser->yytos = yymsp;
  yymsp->stateno = (YYACTIONTYPE)yyact;
  yymsp->major = (YYCODETYPE)yygoto;
#ifdef YYLOCATIONTYPE
  /* P0-NEW-7: commit the LHS location computed before the
  ** action body (and possibly overwritten by the action via
  ** @$ / @<lhsalias>) to the LHS slot's yyloc field. */
  yymsp->yyloc = yyloc_lhs;
#endif
  yyTraceShift(yypParser, yyact, "... then shift");
  return yyact;
}

/*
** The following code executes when the parse fails
*/
#ifndef YYNOERRORRECOVERY
static void yy_parse_failed(
  yyParser *yypParser           /* The parser */
){
  ParseARG_FETCH
  ParseCTX_FETCH
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sFail!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yytos>yypParser->yystack ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser fails */
/************ Begin %parse_failure code ***************************************/
%%
/************ End %parse_failure code *****************************************/
  ParseARG_STORE /* Suppress warning about unused %extra_argument variable */
  ParseCTX_STORE
}
#endif /* YYNOERRORRECOVERY */

/*
** The following code executes when a syntax error first occurs.
**
** Inside the user-supplied %syntax_error block these locals are bound:
**
**   yymajor   -- token code of the offending lookahead.  0 for EOF.
**   yyminor   -- token value (ParseTOKENTYPE) of the offending
**                lookahead.  Also available as the macro TOKEN.
**   yyloc     -- source location of the offending lookahead, when the
**                grammar declares %locations and parsing is driven by
**                ParseLoc().  Also available as the macro TOKEN_LOC.
**                Zero-initialised when locations are not threaded.
**   yypParser -- the parser handle (yyParser *).
**
** These bindings let %syntax_error implementations distinguish, for
** example, "error at end of input" (yymajor==0) from "error at or
** near <token>" (yymajor==some_terminal) the way bison's yyerror
** does via *yychar / *yylloc.
*/
static void yy_syntax_error(
  yyParser *yypParser,           /* The parser */
  int yymajor,                   /* The major type of the error token */
  ParseTOKENTYPE yyminor         /* The minor type of the error token */
#ifdef YYLOCATIONTYPE
  ,YYLOCATIONTYPE yyloc          /* Source location of the error token */
#endif
){
  ParseARG_FETCH
  ParseCTX_FETCH
  /* When the user does not supply a %syntax_error block, yymajor /
  ** yyminor are unused.  Reference them via a (void) cast so generated
  ** parsers compile cleanly under -Wunused-parameter without forcing
  ** an explicit pragma in every emitted file. */
  (void)yymajor;
  (void)yyminor;
#define TOKEN yyminor
#ifdef YYLOCATIONTYPE
#define TOKEN_LOC yyloc
  (void)yyloc;
#endif
/************ Begin %syntax_error code ****************************************/
%%
/************ End %syntax_error code ******************************************/
  ParseARG_STORE /* Suppress warning about unused %extra_argument variable */
  ParseCTX_STORE
}

/*
** The following is executed when the parser accepts
*/
static void yy_accept(
  yyParser *yypParser           /* The parser */
){
  ParseARG_FETCH
  ParseCTX_FETCH
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sAccept!\n",yyTracePrompt);
  }
#endif
#ifndef YYNOERRORRECOVERY
  yypParser->yyerrcnt = -1;
#endif
  assert( yypParser->yytos==yypParser->yystack );
  /* Here code is inserted which will be executed whenever the
  ** parser accepts */
/*********** Begin %parse_accept code *****************************************/
%%
/*********** End %parse_accept code *******************************************/
  ParseARG_STORE /* Suppress warning about unused %extra_argument variable */
  ParseCTX_STORE
}

/* The main parser program.
** The first argument is a pointer to a structure obtained from
** "ParseAlloc" which describes the current state of the parser.
** The second argument is the major token number.  The third is
** the minor token.  The fourth optional argument is whatever the
** user wants (and specified in the grammar) and is available for
** use by the action routines.
**
** Inputs:
** <ul>
** <li> A pointer to the parser (an opaque structure.)
** <li> The major token number.
** <li> The minor token number.
** <li> An option argument of a grammar-specified type.
** </ul>
**
** Outputs:
** None.
*/

/* Helper macro for calling yy_syntax_error with optional location argument.
**
** The yyloc argument is the source location of the *offending lookahead*,
** not of the stack-top symbol.  ParseLoc() stashes the lookahead's
** location in yypParser->yyLookaheadLoc on each call; the macro reads it
** from there.  This matches Bison's *yylloc semantics in yyerror() --
** the location of the token that the parser couldn't accept -- and is
** what callers writing PostgreSQL-compatible error messages need.
**
** When the pull-parser entry point Parse() is used (no location
** threading), or when a syntax error fires before the first ParseLoc()
** call, yyLookaheadLoc is zero-initialized -- callers should treat
** that as "location unknown". */
#ifdef YYLOCATIONTYPE
# define YY_SYNTAX_ERROR(P,M,m) \
    yy_syntax_error(P,M,m, (P)->yyLookaheadLoc)
#else
# define YY_SYNTAX_ERROR(P,M,m) yy_syntax_error(P,M,m)
#endif

/*
** Action-time lookahead access (P0-NEW-5).  Equivalent to bison's
** yychar / yyclearin from inside an action body.
**
** Use case: empty productions whose action wants to consume the
** parser's pending lookahead and reach into the lexer for further
** tokens.  PostgreSQL's plpgsql pl_gram.y has a dozen rules of the
** form
**     decl_datatype : empty { $$ = read_datatype(yychar, ...); yyclearin; }
** that all rely on this idiom.
**
** Parse_get_lookahead() returns the externally-visible token code
** the parser is about to shift -- the same value the caller passed
** to Parse() / ParseLoc() and the same value the user-emitted
** #defines compare against.  Returns YYEMPTY (-2) if no Parse() call
** is currently in progress.  yyminor_out and yyloc_out are optional;
** pass NULL to skip.  yyloc_out is only written when the parser was
** built with %locations (YYLOCATIONTYPE defined); otherwise it is
** ignored.
**
** Parse_clear_lookahead() tells the dispatch loop the action body
** consumed the lookahead via Parse_get_lookahead() and pulled
** further tokens from the lexer itself.  The current Parse() call
** returns without shifting.  The driver's next Parse() call provides
** whatever the lexer has advanced to, treated as a fresh lookahead.
*/
int Parse_get_lookahead(
  void *yyp,                          /* The parser */
  ParseTOKENTYPE *yyminor_out         /* (out) Token value, or NULL */
#ifdef YYLOCATIONTYPE
  , YYLOCATIONTYPE *yyloc_out         /* (out) Token location, or NULL */
#endif
){
  yyParser *yypParser = (yyParser*)yyp;
  if( yyminor_out!=0 ){
    *yyminor_out = yypParser->yyLookaheadMinor;
  }
#ifdef YYLOCATIONTYPE
  if( yyloc_out!=0 ){
    *yyloc_out = yypParser->yyLookaheadLoc;
  }
#endif
  return yypParser->yyLookaheadMajor;
}

void Parse_clear_lookahead(void *yyp){
  yyParser *yypParser = (yyParser*)yyp;
  yypParser->yyLookaheadCleared = 1;
}

/* P0-NEW-8: opt-in eager default-reduce drain.  See forward-decl
** comment above for the semantics.  Implementation: at each step,
** check if the top state's yy_default[] entry is an unconditional
** reduce (no token-specific entries would override it for any
** lookahead).  If so, fire it via yy_reduce; loop.  Otherwise stop.
**
** Detecting "unconditional" requires scanning the state's region of
** yy_lookahead[] for any entry that matches its position-relative
** token code -- O(YYNTOKEN) per drain step.  This is acceptable for
** the use case (one drain per token in the driver loop); a future
** optimization could emit a per-state bitmap at parser-build time. */
void Parse_drain(void *yyp ParseCTX_PDECL){
  yyParser *yypParser = (yyParser*)yyp;
  ParseCTX_FETCH
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE, "%sDrain entry, top=%d\n",
            yyTracePrompt, yypParser->yytos->stateno);
  }
#endif
  for(;;){
    YYACTIONTYPE state = yypParser->yytos->stateno;
    YYACTIONTYPE def;
    int base, i, unconditional;
    ParseTOKENTYPE dummy_tok;
    unsigned int ruleno;

    /* Case 1: state is a deferred reduce from a prior
    ** SHIFTREDUCE action.  In that case yy_shift stashed
    ** stateno = (real_state) + (YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE)
    ** so the next Parse() call would re-enter the dispatch loop
    ** and fire the reduce on the new lookahead.  Fire it here
    ** instead -- with no lookahead in hand, exactly the
    ** semantics the caller asked for. */
    if( state >= YY_MIN_REDUCE && state <= YY_MAX_REDUCE ){
      ruleno = (unsigned int)(state - YY_MIN_REDUCE);
#ifndef NDEBUG
      if( yyTraceFILE ){
        fprintf(yyTraceFILE,
            "%sDrain firing deferred SHIFTREDUCE rule %u\n",
            yyTracePrompt, ruleno);
      }
#endif
      memset(&dummy_tok, 0, sizeof(dummy_tok));
      yy_reduce(yypParser, ruleno, YYNOCODE, dummy_tok ParseCTX_PARAM);
      continue;
    }

    /* Case 2: state is a normal shift state.  If its default
    ** action is an unconditional reduce (no token-specific
    ** entries would override it for any lookahead), fire that
    ** reduce now.  Otherwise stop -- a real lookahead is
    ** required to disambiguate. */
    if( state > YY_MAX_SHIFT ) break;       /* error/accept etc. */

    def = yy_default[state];
    if( def < YY_MIN_REDUCE || def > YY_MAX_REDUCE ) break;

    if( state > YY_SHIFT_COUNT ){
      /* Out of yy_shift_ofst[] range -- no specific actions
      ** possible, so the default is unconditional. */
      unconditional = 1;
    }else{
      base = yy_shift_ofst[state];
      unconditional = 1;
      for( i=0; i<YYNTOKEN; i++ ){
        int j = base + i;
        if( j>=0 && j<YY_ACTTAB_COUNT && yy_lookahead[j]==(YYCODETYPE)i ){
          unconditional = 0;
          break;
        }
      }
    }
    if( !unconditional ) break;

    ruleno = (unsigned int)(def - YY_MIN_REDUCE);
#ifndef NDEBUG
    if( yyTraceFILE ){
      fprintf(yyTraceFILE,
          "%sDrain firing unconditional default reduce by rule %u\n",
          yyTracePrompt, ruleno);
    }
#endif
    memset(&dummy_tok, 0, sizeof(dummy_tok));
    yy_reduce(yypParser, ruleno, YYNOCODE, dummy_tok ParseCTX_PARAM);
    /* Loop. */
  }
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE, "%sDrain exit, top=%d\n",
            yyTracePrompt, yypParser->yytos->stateno);
  }
#endif
}

void Parse(
  void *yyp,                   /* The parser */
  int yymajor,                 /* The major token code number */
  ParseTOKENTYPE yyminor       /* The value for the token */
  ParseARG_PDECL               /* Optional %extra_argument parameter */
){
  YYMINORTYPE yyminorunion;
  YYACTIONTYPE yyact;   /* The parser action. */
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
  int yyendofinput;     /* True if we are at the end of input */
#endif
#ifdef YYERRORSYMBOL
  int yyerrorhit = 0;   /* True if yymajor has invoked an error */
#endif
  yyParser *yypParser = (yyParser*)yyp;  /* The parser */
  int yymajorExternal;
  ParseCTX_FETCH
  ParseARG_STORE

  /* Stash the external lookahead first thing so action code that
  ** runs in any reduce sequence can read it via
  ** Parse_get_lookahead().  Done before the %first_token range
  ** check so that even %syntax_error fired on an out-of-range
  ** code can see the offending token via the same API.  P0-NEW-5. */
  yypParser->yyLookaheadMajor = yymajor;
  yypParser->yyLookaheadMinor = yyminor;
  yypParser->yyLookaheadCleared = 0;

  /* %first_token N -- the user passes the externally-visible token
  ** code (with the offset applied per the emitted #defines).  Convert
  ** to the internal action-table index by subtracting the offset.
  ** EOF (0) is preserved.  When YYFIRSTTOKEN is 0 (the default), this
  ** entire block is a no-op that the optimiser deletes.
  **
  ** If the external code is out of range -- e.g. an ASCII '+' (43)
  ** sneaking through to a parser declared with %first_token 258 --
  ** fire %syntax_error directly and return.  Indexing the action
  ** table at a negative or out-of-range slot would be undefined
  ** behaviour. */
  yymajorExternal = yymajor;
#if YYFIRSTTOKEN > 0
  if( yymajor != 0 ){
    yymajor -= YYFIRSTTOKEN;
    if( yymajor < 0 || yymajor >= YYNTOKEN ){
      YYMINORTYPE yyminorunion_local;
      memset(&yyminorunion_local, 0, sizeof(yyminorunion_local));
      yyminorunion_local.yy0 = yyminor;
#ifndef NDEBUG
      if( yyTraceFILE ){
        fprintf(yyTraceFILE, "%sExternal token code %d outside "
                "[YYFIRSTTOKEN..YYFIRSTTOKEN+YYNTOKEN); rejecting.\n",
                yyTracePrompt, yymajorExternal);
      }
#endif
      YY_SYNTAX_ERROR(yypParser, yymajorExternal, yyminor);
      (void)yyminorunion_local;
      yypParser->yyLookaheadMajor = -2;
      yypParser->yyLookaheadCleared = 0;
      ParseARG_STORE;
      return;
    }
  }
#endif

  assert( yypParser->yytos!=0 );
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
  yyendofinput = (yymajor==0);
#endif

  yyact = yypParser->yytos->stateno;
#ifndef NDEBUG
  if( yyTraceFILE ){
    if( yyact < YY_MIN_REDUCE ){
      fprintf(yyTraceFILE,"%sInput '%s' in state %d\n",
              yyTracePrompt,yyTokenName[yymajor],yyact);
    }else{
      fprintf(yyTraceFILE,"%sInput '%s' with pending reduce %d\n",
              yyTracePrompt,yyTokenName[yymajor],yyact-YY_MIN_REDUCE);
    }
  }
#endif

  while(1){ /* Exit by "break" */
    assert( yypParser->yytos>=yypParser->yystack );
    assert( yyact==yypParser->yytos->stateno );
    yyact = yy_find_shift_action((YYCODETYPE)yymajor,yyact);
    if( yyact >= YY_MIN_REDUCE ){
      unsigned int yyruleno = yyact - YY_MIN_REDUCE; /* Reduce by this rule */
#ifndef NDEBUG
      assert( yyruleno<(int)(sizeof(yyRuleName)/sizeof(yyRuleName[0])) );
      if( yyTraceFILE ){
        int yysize = yyRuleInfoNRhs[yyruleno];
        if( yysize ){
          fprintf(yyTraceFILE, "%sReduce %d [%s]%s, pop back to state %d.\n",
            yyTracePrompt,
            yyruleno, yyRuleName[yyruleno],
            yyruleno<YYNRULE_WITH_ACTION ? "" : " without external action",
            yypParser->yytos[yysize].stateno);
        }else{
          fprintf(yyTraceFILE, "%sReduce %d [%s]%s.\n",
            yyTracePrompt, yyruleno, yyRuleName[yyruleno],
            yyruleno<YYNRULE_WITH_ACTION ? "" : " without external action");
        }
      }
#endif /* NDEBUG */

      /* Check that the stack is large enough to grow by a single entry
      ** if the RHS of the rule is empty.  This ensures that there is room
      ** enough on the stack to push the LHS value */
      if( yyRuleInfoNRhs[yyruleno]==0 ){
#ifdef YYTRACKMAXSTACKDEPTH
        if( (int)(yypParser->yytos - yypParser->yystack)>yypParser->yyhwm ){
          yypParser->yyhwm++;
          assert( yypParser->yyhwm ==
                  (int)(yypParser->yytos - yypParser->yystack));
        }
#endif
        if( yypParser->yytos>=yypParser->yystackEnd ){
          if( yyGrowStack(yypParser) ){
            yyStackOverflow(yypParser);
            break;
          }
        }
      }
      yyact = yy_reduce(yypParser,yyruleno,yymajor,yyminor ParseCTX_PARAM);
      /* If the reduce action called Parse_clear_lookahead() the user
      ** action consumed the token via Parse_get_lookahead() and read
      ** further tokens from the lexer themselves.  Skip the rest of
      ** the dispatch (no shift) and return.  The driver's next
      ** Parse() call provides whatever the lexer has advanced to.
      ** P0-NEW-5. */
      if( yypParser->yyLookaheadCleared ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE,
            "%sLookahead consumed by action; returning without shift.\n",
            yyTracePrompt);
        }
#endif
        yypParser->yyLookaheadMajor = -2;
        yypParser->yyLookaheadCleared = 0;
        ParseARG_STORE;
        return;
      }
    }else if( yyact <= YY_MAX_SHIFTREDUCE ){
      yy_shift(yypParser,yyact,(YYCODETYPE)yymajor,yyminor);
#ifndef YYNOERRORRECOVERY
      yypParser->yyerrcnt--;
#endif
      break;
    }else if( LIME_UNLIKELY(yyact==YY_ACCEPT_ACTION) ){
      yypParser->yytos--;
      yy_accept(yypParser);
      return;
    }else{
      assert( yyact == YY_ERROR_ACTION );
      yyminorunion.yy0 = yyminor;
#ifdef YYERRORSYMBOL
      int yymx;
#endif
#ifndef NDEBUG
      if( yyTraceFILE ){
        fprintf(yyTraceFILE,"%sSyntax Error!\n",yyTracePrompt);
      }
#endif
#ifdef YYERRORSYMBOL
      /* A syntax error has occurred.
      ** The response to an error depends upon whether or not the
      ** grammar defines an error token "ERROR".  
      **
      ** This is what we do if the grammar does define ERROR:
      **
      **  * Call the %syntax_error function.
      **
      **  * Begin popping the stack until we enter a state where
      **    it is legal to shift the error symbol, then shift
      **    the error symbol.
      **
      **  * Set the error count to three.
      **
      **  * Begin accepting and shifting new tokens.  No new error
      **    processing will occur until three tokens have been
      **    shifted successfully.
      **
      */
      if( yypParser->yyerrcnt<0 ){
        /* yymajorExternal is the offset-corrected token code that the
        ** caller passed to Parse(); user %syntax_error code expects to
        ** see this rather than the internal action-table index. */
        YY_SYNTAX_ERROR(yypParser,yymajorExternal,yyminor);
      }
      yymx = yypParser->yytos->major;
      if( yymx==YYERRORSYMBOL || yyerrorhit ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE,"%sDiscard input token %s\n",
             yyTracePrompt,yyTokenName[yymajor]);
        }
#endif
        yy_destructor(yypParser, (YYCODETYPE)yymajor, &yyminorunion);
        yymajor = YYNOCODE;
      }else{
        while( yypParser->yytos > yypParser->yystack ){
          yyact = yy_find_reduce_action(yypParser->yytos->stateno,
                                        YYERRORSYMBOL);
          if( yyact<=YY_MAX_SHIFTREDUCE ) break;
          yy_pop_parser_stack(yypParser);
        }
        if( yypParser->yytos <= yypParser->yystack || yymajor==0 ){
          yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
          yy_parse_failed(yypParser);
#ifndef YYNOERRORRECOVERY
          yypParser->yyerrcnt = -1;
#endif
          yymajor = YYNOCODE;
        }else if( yymx!=YYERRORSYMBOL ){
          yy_shift(yypParser,yyact,YYERRORSYMBOL,yyminor);
        }
      }
      yypParser->yyerrcnt = 3;
      yyerrorhit = 1;
      if( yymajor==YYNOCODE ) break;
      yyact = yypParser->yytos->stateno;
#elif defined(YYNOERRORRECOVERY)
      /* If the YYNOERRORRECOVERY macro is defined, then do not attempt to
      ** do any kind of error recovery.  Instead, simply invoke the syntax
      ** error routine and continue going as if nothing had happened.
      **
      ** Applications can set this macro (for example inside %include) if
      ** they intend to abandon the parse upon the first syntax error seen.
      */
      YY_SYNTAX_ERROR(yypParser,yymajorExternal,yyminor);
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      break;
#else  /* YYERRORSYMBOL is not defined */
      /* This is what we do if the grammar does not define ERROR:
      **
      **  * Report an error message, and throw away the input token.
      **
      **  * If the input token is $, then fail the parse.
      **
      ** As before, subsequent error messages are suppressed until
      ** three input tokens have been successfully shifted.
      **
      ** If YYERRORSYNC is defined, use panic-mode recovery:
      ** discard tokens until a synchronization token is found,
      ** then resume parsing from the current state.
      */
      if( yypParser->yyerrcnt<=0 ){
        YY_SYNTAX_ERROR(yypParser,yymajorExternal,yyminor);
      }
      yypParser->yyerrcnt = 3;
#ifdef YYERRORSYNC
      /* Panic-mode recovery: discard tokens until we find a sync token */
      if( yymajor!=0 && (YYCODETYPE)yymajor<(YYCODETYPE)(sizeof(yy_is_sync_token)/sizeof(yy_is_sync_token[0]))
          && yy_is_sync_token[(YYCODETYPE)yymajor] ){
        /* Current token IS a sync token -- resume parsing here */
        break;
      }
      /* Not a sync token -- discard and continue */
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      yymajor = YYNOCODE;
      break;
#else /* !YYERRORSYNC */
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      if( yyendofinput ){
        yy_parse_failed(yypParser);
#ifndef YYNOERRORRECOVERY
        yypParser->yyerrcnt = -1;
#endif
      }
      break;
#endif /* YYERRORSYNC */
#endif /* YYERRORSYMBOL */
    }
  }
#ifndef NDEBUG
  if( yyTraceFILE ){
    yyStackEntry *i;
    char cDiv = '[';
    fprintf(yyTraceFILE,"%sReturn. Stack=",yyTracePrompt);
    for(i=&yypParser->yystack[1]; i<=yypParser->yytos; i++){
      fprintf(yyTraceFILE,"%c%s", cDiv, yyTokenName[i->major]);
      cDiv = ' ';
    }
    fprintf(yyTraceFILE,"]\n");
  }
#endif
  /* Clear action-time lookahead state on Parse() exit (P0-NEW-5).
  ** Calls to Parse_get_lookahead() outside an active Parse() are
  ** defined to return YYEMPTY. */
  yypParser->yyLookaheadMajor = -2;
  yypParser->yyLookaheadCleared = 0;
  return;
}

#ifdef YYLOCATIONTYPE
/*
** Location-aware version of Parse().
** Same as Parse() but also stores the source location for the shifted token.
** Use this entry point when the %locations directive is active.
**
** The location is stored on the parse stack alongside the token value.
** During reductions, a YYLOC(N) macro can access the location of the
** Nth RHS symbol, and the result location is computed by merging the
** locations of the first and last RHS symbols.
*/
void ParseLoc(
  void *yyp,                   /* The parser */
  int yymajor,                 /* The major token code number */
  ParseTOKENTYPE yyminor,      /* The value for the token */
  YYLOCATIONTYPE yyloc         /* Source location of the token */
  ParseARG_PDECL               /* Optional %extra_argument parameter */
){
  yyParser *yypParser = (yyParser*)yyp;
  /* Stash the lookahead's location so %syntax_error can see it via
  ** yyLookaheadLoc / TOKEN_LOC even when the parser fails on this
  ** token (no shift happens, so yytos->yyloc would still be the
  ** previously-shifted token's location). */
  yypParser->yyLookaheadLoc = yyloc;
  Parse(yyp, yymajor, yyminor ParseARG_PARAM);
  /* After Parse shifts the token, patch the location on top of stack */
  if( yypParser->yytos > yypParser->yystack ){
    yypParser->yytos->yyloc = yyloc;
  }
}
#endif /* YYLOCATIONTYPE */

/*
** Return the fallback token corresponding to canonical token iToken, or
** 0 if iToken has no fallback.
*/
int ParseFallback(int iToken){
#ifdef YYFALLBACK
  assert( iToken<(int)(sizeof(yyFallback)/sizeof(yyFallback[0])) );
  return yyFallback[iToken];
#else
  (void)iToken;
  return 0;
#endif
}

/*
** Return a heap-allocated string listing the tokens that would be
** valid in the current parser state.  The caller must free() the
** returned string.  Returns NULL if the parser is NULL or on
** allocation failure.
**
** The output is a comma-separated list like: "SELECT, INSERT, UPDATE".
** This is useful for producing human-readable error messages.
**
** See also ParseExpectedTokens() for a structured (array) form and
** ParseTokenName() for looking up individual token names.
*/
char *ParseExpectedTokensString(void *yyp){
  yyParser *yypParser = (yyParser*)yyp;
  YYACTIONTYPE stateno;
  char *buf = 0;
  size_t buf_len = 0;
  size_t buf_cap = 0;
  int first = 1;
  int i;
  if( yypParser==0 || yypParser->yytos==0 ) return 0;
  stateno = yypParser->yytos->stateno;

  for(i=0; i<YYNTOKEN; i++){
    YYACTIONTYPE act;
#if YY_SHIFT_COUNT >= 0
    int j = i + yy_shift_ofst[stateno];
    if( j>=0 && j<YY_ACTTAB_COUNT && yy_lookahead[j]==(YYCODETYPE)i ){
      act = yy_action[j];
    }else{
      act = yy_default[stateno];
    }
#else
    act = yy_default[stateno];
#endif
    if( act!=YY_ERROR_ACTION && act!=yy_default[stateno] ){
      const char *name = yyTokenName[i];
      size_t nlen = 0;
      const char *p = name;
      size_t needed;
      while( *p ) { nlen++; p++; }

      /* Need space for ", " + name + NUL */
      needed = buf_len + (first ? 0 : 2) + nlen + 1;
      if( needed > buf_cap ){
        size_t new_cap = (buf_cap == 0) ? 128 : buf_cap * 2;
        char *tmp;
        if( new_cap < needed ) new_cap = needed;
        tmp = (char*)realloc(buf, new_cap);
        if( tmp==0 ){
          free(buf);
          return 0;
        }
        buf = tmp;
        buf_cap = new_cap;
      }
      if( !first ){
        buf[buf_len++] = ',';
        buf[buf_len++] = ' ';
      }
      memcpy(buf + buf_len, name, nlen);
      buf_len += nlen;
      buf[buf_len] = 0;
      first = 0;
    }
  }
  return buf;
}


/* ====================================================================
**  RFC 0059 Diagnostics API
**  --------------------------------------------------------------------
**  These three functions let a host produce rich error messages:
**
**    ParseTokenName(code)      -> string name for a token code
**    ParseState(parser)        -> current parser state number
**    ParseExpectedTokens(...)  -> array of token codes valid at a state
**
**  Together with Token.length from the tokenizer, this is enough to
**  build Rust-compiler-style diagnostics with caret underlines and
**  "expected one of ..." hints.
** ================================================================== */

/*
** Return the string name of a terminal token code, or NULL if the
** code is out of range.  Names come from the yyTokenName[] table.
*/
const char *ParseTokenName(int tokenCode){
  if( tokenCode < 0 || tokenCode >= (int)YYNTOKEN ) return 0;
  return yyTokenName[tokenCode];
}

/*
** Return the current parser state number, or -1 if the parser handle
** is invalid.  For a freshly-initialized parser, returns 0 (the
** initial/start state).
**
** The returned value is meaningful only as an input to
** ParseExpectedTokens().  Host code should not attach semantic meaning
** to the number itself -- it is an internal identifier and may change
** whenever the grammar is regenerated.
*/
int ParseState(void *yyp){
  yyParser *yypParser = (yyParser*)yyp;
  if( yypParser==0 || yypParser->yytos==0 ) return -1;
  return (int)yypParser->yytos->stateno;
}

/*
** Fill `out` with the token codes that would be valid at state
** `stateno` and return the count written (up to `max`).  If `out` is
** NULL or `max` is 0, returns the count that would have been written,
** which lets the caller size a buffer correctly on a second call.
**
** The returned codes can be passed to ParseTokenName() to obtain
** human-readable names.  Codes are returned in ascending numeric
** order.
**
** Typical usage from a %syntax_error callback:
**
**   int s = ParseState(yypParser);
**   int n = ParseExpectedTokens(s, NULL, 0);
**   int *codes = malloc(n * sizeof(int));
**   ParseExpectedTokens(s, codes, n);
**   for( int i = 0; i < n; i++ ){
**       fprintf(stderr, "  expected: %s\n", ParseTokenName(codes[i]));
**   }
**   free(codes);
*/
int ParseExpectedTokens(int stateno, int *out, int max){
  int count = 0;
  int i;

  if( stateno < 0 ) return 0;
#if YY_SHIFT_COUNT >= 0
  if( stateno > YY_SHIFT_COUNT ) return 0;
#endif

  for(i = 0; i < YYNTOKEN; i++){
    YYACTIONTYPE act;
#if YY_SHIFT_COUNT >= 0
    int j = i + yy_shift_ofst[stateno];
    if( j >= 0 && j < YY_ACTTAB_COUNT && yy_lookahead[j] == (YYCODETYPE)i ){
      act = yy_action[j];
    }else{
      act = yy_default[stateno];
    }
#else
    act = yy_default[stateno];
#endif
    if( act != YY_ERROR_ACTION && act != yy_default[stateno] ){
      if( out && count < max ){
        out[count] = i;
      }
      count++;
    }
  }
  return count;
}

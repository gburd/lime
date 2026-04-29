# LALR(1) Parsing Algorithm

This document explains the LALR(1) parsing algorithm as implemented by the
Lime parser generator. It covers the theoretical foundations of LR parsing,
the specific variant Lime uses, and the complete pipeline from grammar
specification to executable parser tables.

## Table of Contents

1. [Introduction to LR Parsing](#introduction-to-lr-parsing)
2. [The LR Parsing Family](#the-lr-parsing-family)
3. [LALR(1) vs LL(k) Parsing](#lalr1-vs-llk-parsing)
4. [Lime's Approach](#limes-approach)
5. [The 10-Step Pipeline](#the-10-step-pipeline)
6. [State Machine Construction](#state-machine-construction)
7. [Conflict Resolution](#conflict-resolution)
8. [Table Compression](#table-compression)
9. [Generated Parser Runtime](#generated-parser-runtime)
10. [Performance Characteristics](#performance-characteristics)
11. [References](#references)

---

## Introduction to LR Parsing

LR parsing is a bottom-up parsing technique for deterministic context-free
grammars. The name "LR" stands for:

- **L** -- Left-to-right scanning of input
- **R** -- constructing a Rightmost derivation (in reverse)

An LR parser works by maintaining a stack of states and grammar symbols.
At each step it consults a table indexed by the current state and the
next input token (the "lookahead") to decide whether to:

- **Shift** -- push the current token and a new state onto the stack, then
  advance to the next input token.
- **Reduce** -- pop symbols from the stack according to a production rule,
  then push the left-hand side nonterminal and a new state.
- **Accept** -- the input has been successfully parsed.
- **Error** -- the input does not conform to the grammar.

The parser never backtracks. Every decision is made in constant time by a
table lookup. This makes LR parsers extremely efficient: parsing time is
linear in the length of the input.

### A Shift-Reduce Example

Consider the grammar for simple addition:

```
expr ::= expr PLUS term.
expr ::= term.
term ::= NUMBER.
```

Parsing the input `3 + 5`:

```
Stack                 Input          Action
-----                 -----          ------
                      3 + 5 $        Shift NUMBER
NUMBER                + 5 $          Reduce term ::= NUMBER
term                  + 5 $          Reduce expr ::= term
expr                  + 5 $          Shift PLUS
expr PLUS             5 $            Shift NUMBER
expr PLUS NUMBER      $              Reduce term ::= NUMBER
expr PLUS term        $              Reduce expr ::= expr PLUS term
expr                  $              Accept
```

The parser reads tokens left-to-right and builds the parse tree from the
leaves up to the root -- hence "bottom-up."

---

## The LR Parsing Family

There are several variants of LR parsing, each representing a different
trade-off between parser table size and grammar recognition power.

### LR(0)

The simplest variant. The parser makes shift/reduce decisions based solely
on the current state, without examining any lookahead token. Very few
practical grammars are LR(0) because most require at least one token of
lookahead to decide between shifting and reducing.

An LR(0) **item** (also called a **configuration** in Lime's terminology)
is a production rule with a dot indicating how much of the rule has been
recognized:

```
expr ::= expr . PLUS term      (dot after first symbol)
expr ::= expr PLUS . term      (dot after second symbol)
expr ::= expr PLUS term .      (dot at end -- ready to reduce)
```

### SLR(1) -- Simple LR

Uses one token of lookahead, but computes the lookahead from the grammar's
FOLLOW sets rather than from the specific parsing context. SLR(1) uses the
same state machine as LR(0) but adds a lookahead check for reduce actions:
a reduce by rule `A ::= alpha` is valid only when the lookahead is in
FOLLOW(A).

SLR(1) is strictly more powerful than LR(0) but still fails on some
grammars that are unambiguous and deterministic.

### LALR(1) -- Look-Ahead LR

The variant implemented by Lime (and also by yacc and bison). LALR(1) uses
the same number of states as SLR(1) -- that is, the LR(0) state machine --
but computes more precise lookahead sets for each configuration. Instead of
using the global FOLLOW set, LALR(1) computes a **specific** follow set for
each configuration in each state, based on the actual parsing contexts that
can reach that configuration.

This means LALR(1) can handle strictly more grammars than SLR(1). In
practice, LALR(1) handles nearly all programming language grammars.

The key insight is that two configurations that appear identical (same rule,
same dot position) but in different states may have different valid
lookahead tokens. LALR(1) tracks this distinction.

### Canonical LR(1)

The most powerful single-token-lookahead variant. Each state tracks the
full lookahead set for every item, so two items that differ only in their
lookahead sets are placed in separate states. This produces the maximum
number of states and the largest tables, but can parse any deterministic
context-free language with one token of lookahead.

Canonical LR(1) parsers are rarely used in practice because they produce
dramatically more states than LALR(1) for most grammars, with little
practical benefit.

### Comparison

```
                     Grammar     Number of    Lookahead
    Variant          Power       States       Precision
    -------          -----       ------       ---------
    LR(0)            Weakest     Fewest       None
    SLR(1)           Weak        Same as      FOLLOW sets
                                 LR(0)
    LALR(1)          Strong      Same as      Per-state
                                 LR(0)        propagated sets
    Canonical LR(1)  Strongest   Most         Per-item sets
```

LALR(1) occupies the practical sweet spot: it has the same compact state
machine as LR(0)/SLR(1) but resolves almost all conflicts that canonical
LR(1) can resolve.

---

## LALR(1) vs LL(k) Parsing

LL and LR are the two major families of deterministic parsing. They
differ fundamentally in direction: LL parsers work top-down (predicting
which production to expand), while LR parsers work bottom-up (recognizing
which production was completed).

### Comparison Table

| Property | LALR(1) | LL(1) / LL(k) |
|---|---|---|
| Parse direction | Bottom-up | Top-down |
| Derivation | Rightmost (reversed) | Leftmost |
| Left recursion | Handled naturally | Must be eliminated |
| Right recursion | Handled naturally | Handled naturally |
| Grammar transformations | Rarely needed | Often needed (left-factor, eliminate left recursion) |
| Lookahead | 1 token (practical) | 1 or k tokens |
| Empty productions | Handled via FIRST/FOLLOW | Require care (FIRST/FOLLOW conflicts) |
| Error recovery | Harder (stack may be deep) | Easier (stack mirrors parse tree) |
| Operator precedence | Declarative (%left, %right) | Encoded in grammar levels |
| Tool examples | yacc, bison, Lime | ANTLR, JavaCC, recursive descent |
| Grammar class | Strictly larger than LL(1) | Subset of LALR(1) |

### Why LALR(1)?

Every LL(1) grammar is also LALR(1), but not vice versa. LALR(1) can
handle:

- **Left-recursive grammars** -- the natural way to express left-associative
  operators like `expr ::= expr PLUS term`. LL parsers cannot handle left
  recursion at all and require it to be rewritten.

- **Larger grammar class** -- some grammars that are LL(k) for large k are
  LALR(1), meaning the LR approach needs less lookahead.

- **Declarative precedence** -- operator precedence and associativity can be
  declared separately from the grammar rules, keeping the grammar clean.
  LL parsers must encode precedence through a hierarchy of grammar rules
  (e.g., expr, term, factor, primary).

The main advantages of LL parsing are:

- **Simpler to write by hand** -- recursive descent parsers are essentially
  hand-coded LL parsers and are straightforward to write and debug.

- **Better error messages** -- because the parser stack directly mirrors
  the structure of what is being parsed, it is easier to produce
  context-sensitive error messages.

- **Easier to integrate actions** -- semantic actions in LL parsers run at
  predictable points during the parse, while LR parsers defer actions until
  a reduction.

For generated parsers (as opposed to hand-written ones), LALR(1) is the
standard choice. Lime generates LALR(1) parsers.

---

## Lime's Approach

Lime follows the classical LALR(1) construction algorithm, closely related
to the method described in the "Dragon Book" (Aho, Sethi, Ullman). It
differs from yacc and bison in several respects:

### Key Differences from Yacc/Bison

1. **Non-terminal names start with lowercase, terminals with uppercase.**
   Yacc uses the opposite convention. Lime infers the symbol type from the
   first character of the name.

2. **Rules use `::=` instead of `:`.**  This is a syntactic convention
   borrowed from BNF notation.

3. **Each RHS symbol can have an alias** (e.g., `expr(A) ::= expr(B) PLUS
   term(C).`), which names the semantic value of that symbol for use in the
   action code. Yacc uses positional `$1`, `$2`, etc.

4. **Destructor declarations.** Lime allows `%destructor` directives that
   specify code to run when a symbol is popped from the stack during error
   recovery. This helps prevent memory leaks.

5. **Single-file output.** Lime generates a single `.c` file and a single
   `.h` file. The template-based output system (via `limpar.c`) means the
   generated code is self-contained and easy to integrate.

6. **No global variables.** The generated parser uses no global state. All
   state is passed through a parser context pointer, making the parser
   thread-safe and reentrant by default.

7. **Table compression with SHIFTREDUCE.** Lime combines a shift followed
   by a reduce into a single SHIFTREDUCE action when the target state has
   only one possible action. This eliminates unnecessary state transitions.

### Internal Terminology

Lime uses some terminology that differs from textbook conventions:

| Lime Term | Textbook Term |
|---|---|
| Configuration | LR item |
| Basis configuration | Kernel item |
| Configuration closure | Item set closure |
| Follow set (on config) | Lookahead set (on item) |
| Propagation link | Lookahead propagation |

---

## The 10-Step Pipeline

The `main()` function in `lime.c` (line 2048) orchestrates the parser
generation through a sequence of well-defined phases. Each phase builds
on the results of the previous one.

```
Grammar File (.y)
      |
      v
  [1. Parse]
      |
      v
  [2. FindRulePrecedences]
      |
      v
  [3. FindFirstSets]
      |
      v
  [4. FindStates]
      |
      v
  [5. FindLinks]
      |
      v
  [6. FindFollowSets]
      |
      v
  [7. FindActions]
      |
      v
  [8. CompressTables]
      |
      v
  [9. ResortStates]
      |
      v
  [10. ReportTable]
      |
      v
  Generated Parser (.c, .h)
```

### Step 1: Parse

**Function**: `Parse()` (lime.c)
**Purpose**: Read the grammar file and build the symbol and rule tables.

The parser reads the `.y` file and processes:
- Production rules (`lhs ::= rhs1 rhs2 ... .`)
- Directive declarations (`%token_type`, `%left`, `%right`, `%nonassoc`,
  `%name`, `%include`, etc.)
- Preprocessor directives (`%ifdef`, `%ifndef`, `%endif`)
- C action code blocks (`{ ... }`)

Output: A populated `struct lime` with linked lists of `struct rule` and
a symbol table of `struct symbol` entries. Each rule records its left-hand
side, right-hand side symbols, action code, and line number.

### Step 2: FindRulePrecedences

**Function**: `FindRulePrecedences()` (lime.c:950)
**Purpose**: Assign a precedence symbol to each production rule.

For rules that do not have an explicit `%prec` directive, the precedence is
inherited from the rightmost terminal symbol in the rule's right-hand side
that has a defined precedence. This is the same convention used by yacc.

```c
// Pseudocode
for each rule rp:
    if rp has no explicit precedence symbol:
        scan rp->rhs left to right
        assign precedence from the rightmost terminal with prec >= 0
```

If no terminal in the rule has a defined precedence, the rule has no
precedence and cannot participate in precedence-based conflict resolution.

### Step 3: FindFirstSets

**Function**: `FindFirstSets()` (lime.c:979)
**Purpose**: Compute FIRST sets and lambda (nullable) flags for all nonterminals.

The FIRST set of a nonterminal A is the set of terminal symbols that can
appear as the first symbol of any string derived from A. A nonterminal is
"lambda" (nullable) if it can derive the empty string.

The algorithm uses a fixed-point iteration:

**Phase 1 -- Compute lambda flags:**

```
repeat:
    for each rule A ::= B1 B2 ... Bn:
        if all of B1..Bn are lambda:
            mark A as lambda
until no changes
```

**Phase 2 -- Compute FIRST sets:**

```
repeat:
    for each rule A ::= B1 B2 ... Bn:
        for i = 1 to n:
            if Bi is a terminal:
                add Bi to FIRST(A)
                break
            else:
                add FIRST(Bi) to FIRST(A)
                if Bi is not lambda:
                    break
until no changes
```

Both phases iterate until a fixed point is reached. Since each iteration
can only add elements (never remove them) and the sets are bounded, the
algorithm always terminates.

FIRST sets are represented as bit vectors (one bit per terminal symbol),
using the `SetNew()`, `SetAdd()`, and `SetUnion()` functions in the `set.c`
module. The bit-vector representation makes set union a fast bitwise-OR
operation.

### Step 4: FindStates

**Function**: `FindStates()` (lime.c:1042)
**Purpose**: Construct all LR(0) states and establish follow-set propagation links.

This is the core of the LALR(1) construction. It builds the LR(0) state
machine (also known as the LR(0) automaton or the "collection of sets of
items").

The algorithm works as follows:

1. Create the **initial state** from the start symbol. The basis of state 0
   contains configurations `[S' ::= . S, {$}]` for every rule with the
   start symbol on the left-hand side. The `$` end-of-input marker is
   seeded into the follow set.

2. For each state, compute its **closure** by adding configurations for
   every nonterminal that appears immediately after a dot. During closure,
   follow-set information is also propagated:
   - If the configuration is `[A ::= alpha . B beta, ...]` and B is a
     nonterminal, add `[B ::= . gamma, FIRST(beta)]` for every rule
     `B ::= gamma`.
   - If `beta` can derive the empty string (all symbols in `beta` are
     lambda), establish a **propagation link** from the parent
     configuration to the new configuration.

3. For each state, **build successor states** by advancing the dot past
   each symbol. All configurations in the current state that have the same
   symbol after the dot contribute to the basis of the successor state.
   This is implemented by `buildshifts()`.

4. Before creating a new state, check the **state table** (a hash table)
   to see if a state with the same basis already exists. If so, merge
   follow-set propagation links into the existing state. If not, create a
   new state and recursively process it.

The process continues until no new states are created. The result is a
complete LR(0) automaton with propagation links threading through it.

```
                  State 0                    State 1
            +-----------------+         +-----------------+
            | S' ::= . expr  |--expr-->| S' ::= expr .  |
            | expr ::= . expr|         |                 |
            |    PLUS term   |         +-----------------+
            | expr ::= . term|
            | term ::= . NUM |
            +--------+--------+
                     |
                    NUM
                     |
                     v
               State 2
            +-----------------+
            | term ::= NUM . |
            +-----------------+
```

### Step 5: FindLinks

**Function**: `FindLinks()` (lime.c:1217)
**Purpose**: Convert backward propagation links into forward links.

During state construction (Step 4), propagation links are recorded as
backward links (from new configuration back to the configuration that
generated it). This step:

1. Annotates every configuration with a pointer to its containing state.
2. Converts all backward links (`bplp`) into forward links (`fplp`).

Forward links are what the follow-set computation needs: when a
configuration's follow set changes, the change propagates forward along
the links to all dependent configurations.

### Step 6: FindFollowSets

**Function**: `FindFollowSets()` (lime.c:1252)
**Purpose**: Propagate follow sets to a fixed point.

This step implements the LALR(1) follow-set computation. It iterates over
all configurations in all states, propagating follow-set information
along the forward links established in Step 5:

```
repeat:
    for each state:
        for each configuration cfp:
            for each forward link plp from cfp:
                union cfp's follow set into plp->target's follow set
                if anything changed, mark target as INCOMPLETE
until no changes
```

The fixed-point iteration terminates because follow sets can only grow
(elements are added, never removed) and are bounded by the number of
terminal symbols.

After this step, every configuration that has its dot at the end of a rule
(i.e., is ready to reduce) has a precise LALR(1) follow set -- the set of
terminals that can legally appear after the rule is reduced, given the
specific parsing contexts that can reach this configuration.

### Step 7: FindActions

**Function**: `FindActions()` (lime.c:1290)
**Purpose**: Build the action table and resolve conflicts.

This step examines every configuration in every state to determine the
parser actions:

1. **Shift actions** were already added during state construction (Step 4).
   When `buildshifts()` creates a transition from state S to state T on
   symbol X, it records a SHIFT action for state S on lookahead X.

2. **Reduce actions** are added here. For every configuration where the dot
   is at the end of the rule (dot == nrhs), a REDUCE action is added for
   every terminal in the configuration's follow set.

3. **The accept action** is added to state 0 on the start nonterminal.

4. **Conflict resolution** is then performed. Actions are sorted by
   lookahead symbol. When two actions have the same lookahead, they are in
   conflict. The `resolve_conflict()` function attempts to resolve each
   conflict using precedence and associativity rules (see
   [Conflict Resolution](#conflict-resolution) below).

5. **Unreachable rule detection**: after all actions are determined, the
   algorithm checks for rules that can never be reduced and reports them
   as errors.

### Step 8: CompressTables

**Function**: `CompressTables()` (lime.c:6616)
**Purpose**: Reduce table size through default actions and action merging.

This step applies three optimizations:

**Default reduce actions**: For each state, find the most common reduce
action. Make it the default action for that state (stored in
`yy_default[]`). All other entries for that reduce action are marked
`NOT_USED`, shrinking the per-state action list. If a state would use
the wildcard token, no default is applied to preserve correct error
detection.

**Auto-reduce states**: If a state's only possible actions are reduces
by a single rule (after defaults are applied), the state is marked as
an "auto-reduce" state.

**SHIFTREDUCE optimization**: Any SHIFT action that transitions to an
auto-reduce state is converted to a SHIFTREDUCE action. This combines
two operations (shift then immediately reduce) into one, avoiding the
overhead of entering and immediately leaving a state:

```
Before:  SHIFT to state 7  (state 7 always reduces by rule 3)
After:   SHIFTREDUCE by rule 3
```

**Single-RHS optimization**: If a SHIFTREDUCE targets a rule with a
single right-hand-side symbol and no associated C action code, the
action is further simplified. The shift-reduce-goto chain collapses to
a direct transition to the ultimate target state.

### Step 9: ResortStates

**Function**: `ResortStates()` (lime.c:6756)
**Purpose**: Reorder states to minimize table size.

States are sorted so that states with more actions appear first (lower
state numbers). Since the action table packing algorithm places entries
for lower-numbered states first, putting busy states early gives them the
best chance of finding overlapping entries in the packed table.

State 0 is always kept as state 0 (it is the start state).

States that are pure auto-reduce states (with no explicit actions) are
placed at the end. These "degenerate" states are counted separately as
`nxstate` (the number of non-degenerate states). The generated arrays
`yy_shift_ofst[]` and `yy_reduce_ofst[]` only need entries for the first
`nxstate` states, saving space.

### Step 10: ReportTable

**Function**: `ReportTable()` (lime.c)
**Purpose**: Generate the final C source code and header file.

This step uses the template file (`limpar.c`) to produce the output:

1. **Pack the action table** using the `acttab` module. For each
   non-degenerate state, the terminal actions and nonterminal actions are
   inserted into a shared `yy_action[]` array, with the `acttab_insert()`
   function finding the optimal placement (see
   [Table Compression](#table-compression)).

2. **Emit the arrays**: `yy_action[]`, `yy_lookahead[]`, `yy_shift_ofst[]`,
   `yy_reduce_ofst[]`, `yy_default[]`, and `yy_reduce_rule[]`.

3. **Emit the semantic action switch**: a `switch` statement with a `case`
   for each rule that has associated C code.

4. **Emit supporting code**: the stack management, error recovery logic,
   and the public API functions (`Parse`, `ParseAlloc`, `ParseFree`,
   etc.).

---

## State Machine Construction

The state machine (LR(0) automaton) is the core data structure produced by
Lime. Understanding its construction is key to understanding LALR(1)
parsing.

### Configurations (Items)

A **configuration** is a production rule with a dot. In Lime, this is
represented by `struct config`:

```c
struct config {
    struct rule *rp;    // The production rule
    int dot;            // Position of the dot (0 = beginning)
    char *fws;          // Follow-set (bit vector)
    struct plink *fplp; // Forward propagation links
    struct plink *bplp; // Backward propagation links
    struct state *stp;  // Containing state
};
```

A configuration `[A ::= X . Y Z]` means "we have seen X and expect to see
Y Z to complete a reduction by rule A ::= X Y Z."

### Closure

Given a set of **basis configurations** (also called kernel items), the
closure adds all configurations reachable by expanding nonterminals.

If the set contains `[A ::= alpha . B beta]` where B is a nonterminal,
then for every rule `B ::= gamma`, add `[B ::= . gamma]` to the set.

Lime implements closure in `Configlist_closure()` (lime.c:1540). During
closure, it also computes partial follow-set information:

- Terminals from FIRST(beta) are added directly to the new
  configuration's follow set.
- If beta can derive the empty string, a propagation link is established
  so the parent configuration's follow set will propagate to the new
  configuration.

### GOTO Function

The GOTO function maps a (state, symbol) pair to a new state. Given
state S and symbol X, GOTO(S, X) is the state whose basis consists of
all configurations from S where the dot is advanced past X:

```
GOTO(S, X) = closure({ [A ::= alpha X . beta] |
                        [A ::= alpha . X beta] in S })
```

Lime computes GOTO implicitly in `buildshifts()` (lime.c:1164). For each
symbol X that appears after the dot in some configuration of state S,
it collects all such configurations, advances their dots, and looks up
(or creates) the resulting state.

### State Identity

Two states are considered identical if they have the same basis (kernel)
configurations. Lime uses a hash table (`State_find()`) keyed on the
sorted basis to detect duplicate states efficiently.

When a duplicate is found, the propagation links from the new (duplicate)
state are merged into the existing state. This is how LALR(1) information
flows: the same LR(0) state may be reached from different parsing contexts,
and the follow sets are the union of all contexts.

### Complete Example

Consider this grammar:

```
program ::= expr.
expr    ::= expr PLUS term.
expr    ::= term.
term    ::= LPAREN expr RPAREN.
term    ::= ID.
```

The LR(0) state machine has the following states (showing basis
configurations only):

```
State 0 (start):
    program ::= . expr

State 1:
    program ::= expr .
    expr    ::= expr . PLUS term

State 2:
    expr ::= term .

State 3:
    term ::= LPAREN . expr RPAREN

State 4:
    term ::= ID .

State 5:
    expr ::= expr PLUS . term

State 6:
    term ::= LPAREN expr . RPAREN
    expr ::= expr . PLUS term

State 7:
    expr ::= expr PLUS term .

State 8:
    term ::= LPAREN expr RPAREN .
```

Transitions:

```
State 0 --expr--> State 1
State 0 --term--> State 2
State 0 --LPAREN--> State 3
State 0 --ID--> State 4
State 1 --PLUS--> State 5
State 3 --expr--> State 6
State 3 --term--> State 2
State 3 --LPAREN--> State 3
State 3 --ID--> State 4
State 5 --term--> State 7
State 5 --LPAREN--> State 3
State 5 --ID--> State 4
State 6 --RPAREN--> State 8
State 6 --PLUS--> State 5
```

---

## Conflict Resolution

When two or more actions apply to the same (state, lookahead) pair, the
parser has a **conflict**. Lime detects and resolves conflicts in the
`resolve_conflict()` function (lime.c:1379).

### Types of Conflicts

**Shift/Reduce (SR) conflict**: The parser can either shift the lookahead
token or reduce by some rule. This is the most common kind of conflict.

**Reduce/Reduce (RR) conflict**: The parser can reduce by two different
rules. This usually indicates a grammar design problem.

**Shift/Shift (SS) conflict**: Two shift actions for the same lookahead
in the same state. This is rare and typically arises only with
multi-terminal symbols.

### Precedence-Based Resolution

Lime resolves shift/reduce conflicts using the precedence and associativity
declared by `%left`, `%right`, and `%nonassoc` directives:

1. The **shift** side has the precedence of the lookahead terminal symbol.
2. The **reduce** side has the precedence of the rule being reduced (from
   Step 2: the rightmost terminal with defined precedence, or an explicit
   `%prec` directive).

Resolution rules:

| Condition | Resolution |
|---|---|
| Shift precedence > reduce precedence | Shift wins |
| Shift precedence < reduce precedence | Reduce wins |
| Equal precedence, RIGHT associativity | Shift wins |
| Equal precedence, LEFT associativity | Reduce wins |
| Equal precedence, NONASSOC | Error action (parse fails) |
| No precedence defined for either | Conflict (reported as warning) |

Example from the classic expression grammar:

```
%left PLUS MINUS.
%left TIMES DIVIDE.
```

The rule `expr ::= expr PLUS expr.` with lookahead `TIMES` is resolved
as a shift because `TIMES` has higher precedence than `PLUS`.

The rule `expr ::= expr PLUS expr.` with lookahead `PLUS` is resolved as
a reduce because `PLUS` has left associativity (equal precedence,
left-associative means reduce).

### Reduce/Reduce Resolution

Lime resolves reduce/reduce conflicts using precedence when possible. If
both rules have precedence symbols, the rule with higher precedence wins.
If precedences are equal or undefined, the conflict is reported.

Unlike yacc (which silently resolves RR conflicts by preferring the
earlier rule), Lime reports all reduce/reduce conflicts as errors. This
is a deliberate design choice to avoid hiding grammar ambiguities.

### Conflict Action Types

After resolution, each action is tagged with one of these types:

```c
enum e_action {
    SHIFT,         // Shift the token and go to a new state
    ACCEPT,        // Input is valid; parsing is complete
    REDUCE,        // Reduce by a rule
    ERROR,         // Syntax error
    SSCONFLICT,    // Unresolved shift/shift conflict
    SRCONFLICT,    // Unresolved shift/reduce conflict
    RRCONFLICT,    // Unresolved reduce/reduce conflict
    SH_RESOLVED,   // Shift that lost to a reduce (via precedence)
    RD_RESOLVED,   // Reduce that lost to a shift (via precedence)
    NOT_USED,      // Removed by table compression
    SHIFTREDUCE    // Combined shift+reduce (compression optimization)
};
```

Resolved conflicts (SH_RESOLVED, RD_RESOLVED) are kept in the action list
for reporting purposes but are not emitted into the parser tables. They
appear in the `.out` report file so the grammar author can verify that
precedence resolved conflicts as intended.

---

## Table Compression

The generated parser uses a compact table representation that balances
lookup speed against memory usage. Understanding this representation is
important for understanding the generated code and for performance tuning.

### The Five Arrays

The parser runtime uses five parallel arrays:

| Array | Type | Indexed by | Purpose |
|---|---|---|---|
| `yy_action[]` | int | Computed offset | Action to take (shift/reduce/error) |
| `yy_lookahead[]` | int | Same as yy_action | Expected token for verification |
| `yy_shift_ofst[]` | int | State number | Offset for terminal lookups |
| `yy_reduce_ofst[]` | int | State number | Offset for nonterminal lookups |
| `yy_default[]` | int | State number | Default action when no match |

### Lookup Algorithm

To find the action for state S and terminal token T:

```c
int i = yy_shift_ofst[S] + T;
if (i >= 0 && i < table_size && yy_lookahead[i] == T) {
    action = yy_action[i];    // Specific action found
} else {
    action = yy_default[S];   // Use default action
}
```

For nonterminal GOTO lookups (after a reduce), the same scheme is used
with `yy_reduce_ofst[]` instead of `yy_shift_ofst[]`.

### Action Table Packing (acttab)

The `acttab` module (lime.c:729) implements the action table packing
algorithm. The `yy_action[]` and `yy_lookahead[]` arrays are shared among
all states. Each state's actions are a contiguous range within the array,
but ranges from different states can **overlap** as long as they do not
conflict.

The packing algorithm (in `acttab_insert()`, lime.c:820) works as follows:

1. **Try to find an existing duplicate**: scan the existing table for a
   range that exactly matches the current state's actions. If found,
   reuse it (two states share the same offset).

2. **Try to find a hole**: scan for a gap in the table where the current
   state's actions fit without colliding with existing entries.

3. **Append**: if no suitable position is found, append the new actions
   at the end of the table.

The verification field `yy_lookahead[]` makes this safe: even if two
states share an offset, a false match is detected because the expected
lookahead will not match.

For terminal lookups (`makeItSafe = true`), the offset is chosen so that
any lookahead value -- even an invalid one from erroneous input -- will
index within the array bounds. This prevents out-of-bounds reads. For
nonterminal lookups (`makeItSafe = false`), offsets can be more aggressive
because nonterminals are generated internally and are always valid.

### State Reordering

Before table packing, states are reordered (Step 9) so that states with
more actions come first. Since the packing algorithm processes states in
order, this gives the busiest states the best chance of finding overlapping
positions, leading to a smaller packed table.

Auto-reduce states (states that always reduce by the same rule) are
eliminated from the action table entirely. Their behavior is encoded
as SHIFTREDUCE actions in the states that transition to them, so they
never need to be looked up.

### Space Savings

The combination of these techniques typically reduces table size by
50-80% compared to a naive two-dimensional array. For a grammar with
N states and T terminal symbols, the naive table would require N*T
entries. The packed table is typically O(N + T) entries.

---

## Generated Parser Runtime

The parser generated by Lime is a push-down automaton implemented as a
C function. The generated code follows a standard structure.

### Parser State

```c
struct yyParser {
    yyStackEntry *yytos;       // Top of the parse stack
    yyStackEntry yystack[YYSTACKDEPTH]; // The parse stack
    int yyerrcnt;              // Error recovery counter
};

struct yyStackEntry {
    YYACTIONTYPE stateno;      // State number
    YYCODETYPE major;          // Token/nonterminal code
    YYMINORTYPE minor;         // Semantic value
};
```

### Parse Loop

The core loop of the generated parser:

```
function Parse(parser, token, value):
    push token onto lookahead
    while lookahead is not empty:
        state = stack top
        action = lookup(state, lookahead)

        if action is SHIFT:
            push (new_state, token, value) onto stack
            consume lookahead
        else if action is SHIFTREDUCE:
            push (ignored, token, value) onto stack
            perform reduce (immediately, no table lookup needed)
        else if action is REDUCE:
            execute semantic action for the rule
            pop |rhs| entries from stack
            push (GOTO(stack_top, lhs), lhs, result)
        else if action is ACCEPT:
            return success
        else:
            initiate error recovery
```

### Error Recovery

Lime's error recovery follows the standard yacc approach:

1. Pop states from the stack until finding a state that can shift the
   special `error` token.
2. Shift the `error` token.
3. Discard input tokens until finding one that is valid in the current
   state.
4. Resume normal parsing.

The `%destructor` directives ensure that semantic values popped during
error recovery are properly cleaned up.

---

## Performance Characteristics

### Time Complexity

| Phase | Complexity | Notes |
|---|---|---|
| Parse grammar | O(G) | G = grammar size |
| FindFirstSets | O(R * T) | R = rules, T = terminals; fixed-point |
| FindStates | O(S * C) | S = states, C = avg configurations/state |
| FindFollowSets | O(S * C * T) | Fixed-point over propagation links |
| FindActions | O(S * C * T) | Adding reduce actions + conflict resolution |
| CompressTables | O(S * A) | A = avg actions/state |
| Table packing | O(S * T * N) | N = table size; finding overlaps |
| **Total generation** | **O(S * C * T)** | Dominated by follow-set computation |

### Space Complexity

| Structure | Size | Notes |
|---|---|---|
| States | O(S) | S typically O(R) to O(R^2) |
| Configurations | O(S * C) | C is rule length dependent |
| FIRST sets | O(N * T) bits | N nonterminals, T terminals |
| Follow sets | O(S * C * T) bits | One set per configuration |
| Packed tables | O(S + T) | After compression |

### Runtime Parsing Performance

The generated parser runs in:

- **Time**: O(n) where n is the number of input tokens. Each token
  requires exactly one table lookup (constant time) and at most one
  reduce chain. The reduce chain is bounded by the grammar, not the input.

- **Space**: O(n) in the worst case for the parse stack (e.g., deeply
  right-recursive grammars). In practice, the stack depth is proportional
  to the nesting depth of the input, which is typically O(log n) or O(1)
  for most programming languages.

The table-driven approach means the generated parser has a very small code
footprint and excellent cache behavior. The action table lookup involves
reading three array entries (offset, lookahead, action) which are likely
to be in the L1 cache for active states.

---

## References

### Foundational Works

1. **Knuth, D.E.** "On the Translation of Languages from Left to Right."
   *Information and Control*, 8(6):607-639, 1965.
   -- The original paper introducing LR parsing.

2. **DeRemer, F.L.** "Practical Translators for LR(k) Languages."
   PhD thesis, MIT, 1969.
   -- Introduced SLR and LALR parsing.

3. **DeRemer, F.L. and Pennello, T.** "Efficient Computation of LALR(1)
   Look-Ahead Sets." *ACM Transactions on Programming Languages and
   Systems*, 4(4):615-649, 1982.
   -- The efficient algorithm for computing LALR(1) lookahead sets that
   Lime's propagation-link approach is based on.

### Textbooks

4. **Aho, A.V., Sethi, R., and Ullman, J.D.** *Compilers: Principles,
   Techniques, and Tools* (the "Dragon Book"). Addison-Wesley, 1986.
   Second edition (with Lam, M.S.), 2006.
   -- The standard reference for compiler construction, including
   comprehensive treatment of LALR(1) parsing.

5. **Appel, A.W.** *Modern Compiler Implementation in C/Java/ML.*
   Cambridge University Press, 1998.
   -- Covers LR parsing with a focus on practical implementation.

6. **Grune, D. and Jacobs, C.J.H.** *Parsing Techniques: A Practical
   Guide.* Springer, 2008.
   -- Comprehensive survey of parsing algorithms, including detailed
   comparison of LL and LR variants.

### Related Tools

7. **Johnson, S.C.** "Yacc: Yet Another Compiler-Compiler."
   *Computing Science Technical Report 32*, Bell Laboratories, 1975.
   -- The original LALR(1) parser generator for Unix.

8. **Hipp, D.R.** "The Lemon Parser Generator."
   https://www.hwaci.com/sw/lemon/
   -- The original Lemon parser generator on which Lime is based.

9. **GNU Bison Manual.**
   https://www.gnu.org/software/bison/manual/
   -- The successor to yacc, with extensions for GLR parsing.

### Action Table Compression

10. **Tarjan, R.E. and Yao, A.C.** "Storing a Sparse Table."
    *Communications of the ACM*, 22(11):606-611, 1979.
    -- Theoretical foundations for sparse table representation.

11. **Dencker, P., Durre, K., and Heuft, J.** "Optimization of Parser
    Tables for Portable Compilers." *ACM Transactions on Programming
    Languages and Systems*, 6(4):546-572, 1984.
    -- Techniques for compressing LR parser tables, including the
    row-displacement method used by Lime's `acttab` module.

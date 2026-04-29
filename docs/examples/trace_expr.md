# Parse Trace: `2 + 3 * 4`

This document walks through the complete LALR(1) parsing of the expression
`2 + 3 * 4` using a simple arithmetic grammar. It shows item set
construction, the action/goto tables, and a step-by-step parse execution
trace.

## Grammar

```
(0)  program ::= expr.
(1)  expr    ::= expr PLUS expr.
(2)  expr    ::= expr TIMES expr.
(3)  expr    ::= NUMBER.
```

With precedence declarations (as you would write in a Lime grammar file):

```
%left PLUS.
%left TIMES.
```

`TIMES` has higher precedence than `PLUS` because it is declared later.
Both are left-associative.

## Terminal and Nonterminal Symbols

```
Terminals:     $ (0)   PLUS (1)   TIMES (2)   NUMBER (3)
Nonterminals:  program (4)   expr (5)
```

## Step 1: Item Sets (LR(0) States)

### State 0

Basis:
```
  program ::= . expr
```

Closure (expand `expr`):
```
  program ::= . expr
  expr    ::= . expr PLUS expr
  expr    ::= . expr TIMES expr
  expr    ::= . NUMBER
```

### State 1

Reached from State 0 on `expr`:
```
  program ::= expr .            [follow: {$}]
  expr    ::= expr . PLUS expr  [follow: {$, PLUS, TIMES}]
  expr    ::= expr . TIMES expr [follow: {$, PLUS, TIMES}]
```

### State 2

Reached from State 0 on `NUMBER`:
```
  expr ::= NUMBER .             [follow: {$, PLUS, TIMES}]
```

### State 3

Reached from State 1 on `PLUS`:
```
  expr ::= expr PLUS . expr
```

Closure:
```
  expr ::= expr PLUS . expr
  expr ::= . expr PLUS expr
  expr ::= . expr TIMES expr
  expr ::= . NUMBER
```

### State 4

Reached from State 1 on `TIMES`:
```
  expr ::= expr TIMES . expr
```

Closure:
```
  expr ::= expr TIMES . expr
  expr ::= . expr PLUS expr
  expr ::= . expr TIMES expr
  expr ::= . NUMBER
```

### State 5

Reached from State 3 on `expr`:
```
  expr ::= expr PLUS expr .     [follow: {$, PLUS}]
  expr ::= expr . PLUS expr
  expr ::= expr . TIMES expr
```

Note: the follow set for the reduce item `expr ::= expr PLUS expr .`
is `{$, PLUS}` -- not `TIMES`. This is because `TIMES` has higher
precedence than `PLUS`, so the shift/reduce conflict on `TIMES` is
resolved in favor of shift.

### State 6

Reached from State 4 on `expr`:
```
  expr ::= expr TIMES expr .    [follow: {$, PLUS, TIMES}]
  expr ::= expr . PLUS expr
  expr ::= expr . TIMES expr
```

Note: the follow set for the reduce item includes `TIMES` because the
shift/reduce conflict on `TIMES` is resolved in favor of reduce (left
associativity at equal precedence).

### State 7

Reached from States 3 and 4 on `NUMBER`:
```
  expr ::= NUMBER .             [follow: {$, PLUS, TIMES}]
```

(This is the same as State 2; depending on the implementation, these
may be merged into a single state.)

## Step 2: Action and GOTO Tables

After conflict resolution using the declared precedences:

### Action Table (terminals)

| State | NUMBER | PLUS | TIMES | $ |
|-------|--------|------|-------|---|
| 0 | s2 | | | |
| 1 | | s3 | s4 | accept |
| 2 | | r3 | r3 | r3 |
| 3 | s7 | | | |
| 4 | s7 | | | |
| 5 | | r1 | s4 | r1 |
| 6 | | r2 | r2 | r2 |
| 7 | | r3 | r3 | r3 |

Legend:
- `sN` = shift and go to state N
- `rN` = reduce by rule N
- `accept` = accept the input
- empty = syntax error

### GOTO Table (nonterminals)

| State | program | expr |
|-------|---------|------|
| 0 | | 1 |
| 3 | | 5 |
| 4 | | 6 |

### Key Conflict Resolutions

**State 5, TIMES**: The configuration `expr ::= expr PLUS expr .` wants
to reduce (rule 1), and `TIMES` wants to shift to State 4. Since TIMES
has higher precedence than PLUS, the shift wins. This ensures that
`2 + 3 * 4` is parsed as `2 + (3 * 4)`.

**State 5, PLUS**: The same reduce configuration conflicts with shifting
PLUS. Since PLUS has left associativity (equal precedence), the reduce
wins. This ensures that `2 + 3 + 4` is parsed as `(2 + 3) + 4`.

**State 6, TIMES**: The configuration `expr ::= expr TIMES expr .` wants
to reduce, and TIMES wants to shift. Since TIMES is left-associative at
equal precedence, the reduce wins. This ensures `2 * 3 * 4` is parsed as
`(2 * 3) * 4`.

**State 6, PLUS**: Reduce by `expr ::= expr TIMES expr.` wins over shift
because TIMES has higher precedence than PLUS. This ensures that in
`2 * 3 + 4`, the multiplication binds tighter.

## Step 3: Parse Trace for `2 + 3 * 4`

Input tokens: `NUMBER(2)  PLUS  NUMBER(3)  TIMES  NUMBER(4)  $`

```
Step  Stack                          Input              Action
----  -----                          -----              ------
 1    [0]                            NUMBER(2) + 3*4 $  Shift NUMBER, goto 2
 2    [0 NUMBER:2]                   PLUS 3*4 $         Reduce r3: expr ::= NUMBER
      [0 expr:1]                     PLUS 3*4 $         GOTO(0, expr) = 1
 3    [0 expr:1]                     PLUS 3*4 $         Shift PLUS, goto 3
 4    [0 expr:1 PLUS:3]              NUMBER(3) *4 $     Shift NUMBER, goto 7
 5    [0 expr:1 PLUS:3 NUMBER:7]     TIMES 4 $          Reduce r3: expr ::= NUMBER
      [0 expr:1 PLUS:3 expr:5]       TIMES 4 $          GOTO(3, expr) = 5
 6    [0 expr:1 PLUS:3 expr:5]       TIMES 4 $          Shift TIMES, goto 4
                                                        (TIMES > PLUS, so shift)
 7    [0 expr:1 PLUS:3 expr:5        NUMBER(4) $        Shift NUMBER, goto 7
       TIMES:4]
 8    [0 expr:1 PLUS:3 expr:5        $                  Reduce r3: expr ::= NUMBER
       TIMES:4 NUMBER:7]
      [0 expr:1 PLUS:3 expr:5        $                  GOTO(4, expr) = 6
       TIMES:4 expr:6]
 9    [0 expr:1 PLUS:3 expr:5        $                  Reduce r2: expr ::= expr TIMES expr
       TIMES:4 expr:6]                                  Pop 3 entries (expr, TIMES, expr)
      [0 expr:1 PLUS:3 expr:5]       $                  GOTO(3, expr) = 5
10    [0 expr:1 PLUS:3 expr:5]       $                  Reduce r1: expr ::= expr PLUS expr
                                                        ($ is in follow set, so reduce)
                                                        Pop 3 entries (expr, PLUS, expr)
      [0 expr:1]                     $                  GOTO(0, expr) = 1
11    [0 expr:1]                     $                  Accept
```

### Parse Tree

The reductions build the following parse tree (bottom-up):

```
            expr (step 10)
           / | \
         /   |   \
       expr PLUS  expr (step 9)
        |        / | \
     NUMBER    /   |   \
       2    expr TIMES expr
             |          |
          NUMBER     NUMBER
             3          4
```

The value `2 + 3 * 4 = 14` is computed correctly because the parse tree
reflects the intended precedence: multiplication binds tighter than
addition.

### How Precedence Affected the Trace

The critical decision happens at Step 6. The parser is in State 5 with
the stack `[0 expr:1 PLUS:3 expr:5]` and the lookahead is `TIMES`.

State 5 has two possible actions for `TIMES`:
- Reduce by rule 1 (`expr ::= expr PLUS expr`)
- Shift `TIMES` and go to State 4

Because `TIMES` has higher precedence than `PLUS`, the shift wins. This
means the parser delays the addition, shifting `TIMES` to eventually
evaluate the multiplication first.

If the precedences were reversed (PLUS higher than TIMES), the parser
would reduce at Step 6, giving `(2 + 3) * 4 = 20` instead.

If no precedences were declared, Lime would report a shift/reduce conflict
and the grammar author would need to resolve it.

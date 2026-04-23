# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the **Lime Parser Generator**, an LALR(1) parser generator tool similar to yacc/bison. Lime generates C parsers from grammar specifications. (Based on the original Lemon parser generator.)

## Files

- **lime.c** - Main parser generator program (single-file compilation containing all modules)
- **limpar.c** - Parser template file used to generate output parsers (contains runtime parser code with %% placeholders)
- **tokenize.c** - SQL tokenizer (appears to be from SQLite project)

## Building

Standard C compilation:
```
gcc -o lime lime.c
```

Or with your preferred C compiler:
```
cc -o lime lime.c
clang -o lime lime.c
```

## Usage

Generate a parser from a grammar file:
```
./lime grammar_file.y
```

Common options:
- `-d <dir>` - Specify output directory (default: current directory)
- `-T <file>` - Use custom template file instead of limpar.c
- `-m` - Output makeheaders-compatible file
- `-l` - Don't print #line statements
- `-s` - Print parser statistics to stdout
- `-q` - Quiet mode (don't generate .out report file)
- `-c` - Don't compress action table
- `-p` - Show conflicts resolved by precedence rules
- `-D <macro>` - Define preprocessor macro for %ifdef directives
- `-U <macro>` - Undefine preprocessor macro
- `-x` - Print version and exit

## Architecture

lime.c is organized as a concatenation of multiple modules:

### Core Data Structures (struct.h)

- **struct lime** - Main state containing entire parser generator state (rules, states, symbols, configuration)
- **struct rule** - Production rule with RHS symbols, precedence, and action code
- **struct symbol** - Terminal or non-terminal symbol with type, precedence, fallback info
- **struct state** - LR(0) state containing configurations and actions
- **struct config** - Configuration (dotted production) with lookahead and follow-set info
- **struct plink** - Propagation link for computing follow sets

### Processing Pipeline

The main() function (lime.c:1740) orchestrates the parser generation:

1. **Parse** - Read and parse grammar file
2. **FindRulePrecedences** - Determine precedence for each production
3. **FindFirstSets** - Compute FIRST sets for non-terminals
4. **FindStates** - Build LR(0) automaton states
5. **FindLinks** - Establish follow-set propagation links
6. **FindFollowSets** - Compute FOLLOW sets for configurations
7. **FindActions** - Determine shift/reduce actions for each state
8. **CompressTables** - Compress action table (unless -c flag)
9. **ResortStates** - Reorder states to optimize table size (unless -r flag)
10. **ReportOutput** - Generate .out report file (unless -q flag)
11. **ReportTable** - Generate C source code for parser
12. **ReportHeader** - Generate .h header file with token definitions

### Key Modules

- **action.c** - Action table construction and compression (acttab)
- **build.c** - State construction (FindStates, FindLinks, FindFollowSets, FindActions)
- **configlist.c** - Configuration list management
- **error.c** - Error reporting
- **main.c** - Command-line processing and main pipeline
- **parse.c** - Grammar file parser with preprocessor support
- **report.c** - Output generation (C source, header, report file)
- **set.c** - Bit-set operations for FIRST/FOLLOW sets
- **table.c** - Hash table for symbol/state lookup
- **option.c** - Command-line option parsing

## Template System

The limpar.c template file contains parser runtime code with special markers:
- `%%` lines - Insertion points where Lime adds generated content
- `Parse` identifier prefix - Replaced with %name directive value from grammar
- Template can be overridden with -T option

## Memory Management

All allocations use custom wrappers (lime_malloc, lime_calloc, lime_realloc) that track allocations in a linked list. `lime_free_all()` can clean up all memory at exit to satisfy memory leak detection tools.

## Code Organization

The entire program is intentionally structured as a single-file compilation to make it easy to embed Lime in other project build systems. The comment at lime.c:2 explains this design choice.

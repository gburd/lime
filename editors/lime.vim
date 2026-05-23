" Vim syntax file
" Language:     Lime parser generator grammar
" Maintainer:   Lime Parser Generator Project
" Filenames:    *.lime, *.lex (when used with Lime's -X flag)
" License:      Public Domain
"
" Drop this file under ~/.vim/syntax/ (or .config/nvim/syntax/) and
" add an autocmd to set the filetype:
"
"   autocmd BufRead,BufNewFile *.lime setfiletype lime
"
" or place the same line in ~/.vim/ftdetect/lime.vim.

if exists("b:current_syntax")
  finish
endif

" Comments: /* ... */ block, // ... line.
syn region limeBlockComment start=+/\*+ end=+\*/+ contains=limeTodo
syn match  limeLineComment  +//.*$+ contains=limeTodo
syn keyword limeTodo TODO FIXME XXX NOTE contained

" String and character literals (used in token names like '+' and "abc").
syn region limeString start=+"+ skip=+\\.+ end=+"+
syn region limeString start=+'+ skip=+\\.+ end=+'+

" Embedded C action blocks: { ... }.  We don't try to syntax-highlight
" the C inside; just mark the braces as a special region so users can
" tell action bodies from grammar text at a glance.
syn region limeAction start=+{+ end=+}+ keepend transparent contains=ALLBUT,limeAction

" Directives.  Sourced from `grep -oE 'strcmp\(.*x.*, *"[a-z_]+"' lime.c`;
" keep alphabetical for diff-friendliness.
syn match limeDirective +%\<\(ast_auto\|ast_list\|ast_node\|ast_prefix\)\>+
syn match limeDirective +%\<\(code\|default_destructor\|default_type\|destructor\)\>+
syn match limeDirective +%\<\(error_sync\|expect\|export\|extra_argument\|extra_context\)\>+
syn match limeDirective +%\<\(fallback\|first_token\|free\|from\)\>+
syn match limeDirective +%\<\(ifdef\|ifndef\|else\|endif\)\>+
syn match limeDirective +%\<\(import\|include\)\>+
syn match limeDirective +%\<\(left\|location_type\|locations\)\>+
syn match limeDirective +%\<\(module_description\|module_name\|module_version\)\>+
syn match limeDirective +%\<\(name\|name_prefix\|nonassoc\)\>+
syn match limeDirective +%\<\(parse_accept\|parse_failure\)\>+
syn match limeDirective +%\<\(realloc\|require\|right\)\>+
syn match limeDirective +%\<\(stack_overflow\|stack_size\|stack_size_limit\)\>+
syn match limeDirective +%\<\(start\|start_symbol\|symbol_prefix\|syntax_error\)\>+
syn match limeDirective +%\<\(token\|token_class\|token_destructor\|token_prefix\|token_type\)\>+
syn match limeDirective +%\<\(type\|wildcard\)\>+

" Rule production arrow.
syn match limeArrow +::=+

" Token names: ALL_CAPS identifiers.
syn match limeToken +\<[A-Z_][A-Z0-9_]*\>+

" Aliases attached to grammar symbols, e.g. expr(A).
syn match limeAlias +([A-Za-z_][A-Za-z0-9_]*)+

" Precedence markers like [UMINUS].
syn match limePrecMarker +\[[A-Z_][A-Z0-9_]*\]+

" Highlight links — pick something readable in default colorschemes.
hi def link limeBlockComment Comment
hi def link limeLineComment  Comment
hi def link limeTodo         Todo
hi def link limeString       String
hi def link limeDirective    PreProc
hi def link limeArrow        Keyword
hi def link limeToken        Constant
hi def link limeAlias        Identifier
hi def link limePrecMarker   Special

let b:current_syntax = "lime"

// Test grammar for ParseReset functionality
%name TestReset
%token_type {int}
%extra_argument {int *counter}

%token_destructor { (*counter)++; }

%start_symbol program

program ::= stmt_list.

stmt_list ::= stmt.
stmt_list ::= stmt_list stmt.

stmt ::= NUM.
stmt ::= PLUS.
stmt ::= MINUS.

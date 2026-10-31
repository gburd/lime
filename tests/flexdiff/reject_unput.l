/* flex's examples/manual/reject.lex, with a byte in front of each UNIX:
 * unput() right at the start of the buffer makes stock flex rescan
 * uninitialised buffer memory, so that case is not compared. */
%option noyywrap
%%
UNIX       {
                unput('U'); unput('N'); unput('G'); unput('\0');
                REJECT;
           }
GNU        printf("GNU is Not Unix!\n");
%%
int main(void){ yylex(); return 0; }

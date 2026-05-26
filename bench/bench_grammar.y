/* Bison benchmark grammar: scaled expression language (~250 rules)
**
** Equivalent to bench_grammar.lime for timing comparisons.
*/

%{
#include <stdio.h>
int yylex(void);
void yyerror(const char *s);
%}

%token ID INTEGER FLOAT STRING PARAM
%token SELECT INSERT UPDATE DELETE CREATE DROP ALTER
%token FROM WHERE GROUP BY HAVING ORDER LIMIT OFFSET
%token INTO VALUES SET AS ON JOIN INNER LEFT RIGHT FULL OUTER CROSS NATURAL USING
%token AND OR NOT BETWEEN IN LIKE ILIKE IS DISTINCT EXISTS
%token CASE WHEN THEN ELSE END
%token CAST COALESCE NULLIF GREATEST LEAST
%token TABLE INDEX VIEW FUNCTION TRIGGER SEQUENCE SCHEMA TYPE EXTENSION DOMAIN AGGREGATE
%token IF THEN_KW ELSE_KW ELSIF WHILE FOR LOOP RETURN CALL RAISE EXCEPTION NOTICE
%token BEGIN_KW TRANSACTION COMMIT ROLLBACK SAVEPOINT TO
%token GRANT REVOKE PRIVILEGE ALL
%token WITH RECURSIVE
%token PRIMARY KEY FOREIGN REFERENCES CHECK DEFAULT UNIQUE CONSTRAINT
%token COLUMN ADD RENAME RESTART
%token SHOW EXPLAIN ANALYZE VERBOSE
%token DECLARE
%token BEFORE AFTER INSTEAD OF
%token SFUNC STYPE INITCOND
%token SETOF ARRAY ROW COLLATE TYPECAST
%token NULL_KW TRUE_KW FALSE_KW
%token ASC DESC
%token RETURNS
%token REPLACE
%token DOTDOT

%token SEMI COMMA DOT LPAREN RPAREN LBRACKET RBRACKET COLON

%token EQ NE LT GT LE GE
%token PLUS MINUS STAR SLASH PERCENT

%left  OR
%left  AND
%right NOT
%left  EQ NE
%left  LT GT LE GE
%left  PLUS MINUS
%left  STAR SLASH PERCENT
%right UMINUS
%left  DOT LBRACKET
%left  LPAREN

%%

program: stmt_list;

stmt_list: stmt_list SEMI stmt | stmt;

stmt: select_stmt | insert_stmt | update_stmt | delete_stmt
    | create_stmt | drop_stmt | alter_stmt
    | begin_stmt | commit_stmt | rollback_stmt
    | set_stmt | show_stmt | explain_stmt
    | grant_stmt | revoke_stmt
    | declare_stmt | if_stmt | while_stmt | for_stmt
    | return_stmt | call_stmt | raise_stmt | with_stmt;

/* SELECT */
select_stmt: SELECT select_cols FROM from_clause where_clause
             group_clause having_clause order_clause limit_clause
           | SELECT select_cols FROM from_clause where_clause
           | SELECT select_cols FROM from_clause
           | SELECT select_cols;

select_cols: select_cols COMMA select_col | select_col;
select_col: expr AS ID | expr | STAR;

/* FROM */
from_clause: from_clause COMMA from_item | from_item;
from_item: table_ref | table_ref AS ID
         | LPAREN select_stmt RPAREN AS ID
         | from_item join_type JOIN from_item ON expr
         | from_item join_type JOIN from_item USING LPAREN id_list RPAREN
         | from_item CROSS JOIN from_item
         | from_item NATURAL join_type JOIN from_item;

join_type: INNER | LEFT | LEFT OUTER | RIGHT | RIGHT OUTER
         | FULL | FULL OUTER | /* empty */;

/* Clauses */
where_clause: WHERE expr;
group_clause: GROUP BY expr_list;
having_clause: HAVING expr;
order_clause: ORDER BY order_list;
limit_clause: LIMIT expr | LIMIT expr OFFSET expr | LIMIT expr COMMA expr;

order_list: order_list COMMA order_item | order_item;
order_item: expr ASC | expr DESC | expr;

/* INSERT */
insert_stmt: INSERT INTO table_ref LPAREN id_list RPAREN VALUES value_list_group
           | INSERT INTO table_ref VALUES value_list_group
           | INSERT INTO table_ref LPAREN id_list RPAREN select_stmt
           | INSERT INTO table_ref select_stmt
           | INSERT INTO table_ref DEFAULT VALUES;

value_list_group: value_list_group COMMA LPAREN expr_list RPAREN
                | LPAREN expr_list RPAREN;

/* UPDATE */
update_stmt: UPDATE table_ref SET assignment_list where_clause
           | UPDATE table_ref SET assignment_list;
assignment_list: assignment_list COMMA assignment | assignment;
assignment: ID EQ expr | LPAREN id_list RPAREN EQ LPAREN expr_list RPAREN;

/* DELETE */
delete_stmt: DELETE FROM table_ref where_clause | DELETE FROM table_ref;

/* CREATE */
create_stmt: CREATE TABLE table_ref LPAREN column_def_list RPAREN
           | CREATE TABLE IF NOT EXISTS table_ref LPAREN column_def_list RPAREN
           | CREATE INDEX ID ON table_ref LPAREN id_list RPAREN
           | CREATE UNIQUE INDEX ID ON table_ref LPAREN id_list RPAREN
           | CREATE VIEW ID AS select_stmt
           | CREATE OR REPLACE VIEW ID AS select_stmt
           | CREATE FUNCTION ID LPAREN param_list RPAREN RETURNS type_name AS block_stmt
           | CREATE TRIGGER ID trigger_timing trigger_event ON table_ref block_stmt
           | CREATE SEQUENCE ID
           | CREATE SCHEMA ID
           | CREATE TYPE ID AS LPAREN column_def_list RPAREN
           | CREATE EXTENSION ID
           | CREATE DOMAIN ID AS type_name constraint_list
           | CREATE AGGREGATE ID LPAREN type_name RPAREN LPAREN aggregate_def_list RPAREN;

column_def_list: column_def_list COMMA column_def | column_def;
column_def: ID type_name constraint_list | ID type_name;

constraint_list: constraint_list constraint_item | constraint_item;
constraint_item: NOT NULL_KW | UNIQUE | PRIMARY KEY | DEFAULT expr
               | CHECK LPAREN expr RPAREN
               | REFERENCES table_ref LPAREN id_list RPAREN
               | FOREIGN KEY LPAREN id_list RPAREN REFERENCES table_ref LPAREN id_list RPAREN;

/* DROP */
drop_stmt: DROP TABLE table_ref | DROP TABLE IF EXISTS table_ref
         | DROP INDEX ID | DROP VIEW ID | DROP FUNCTION ID
         | DROP TRIGGER ID | DROP SEQUENCE ID | DROP SCHEMA ID;

/* ALTER */
alter_stmt: ALTER TABLE table_ref ADD COLUMN column_def
          | ALTER TABLE table_ref DROP COLUMN ID
          | ALTER TABLE table_ref ALTER COLUMN ID SET type_name
          | ALTER TABLE table_ref RENAME TO ID
          | ALTER TABLE table_ref ADD constraint_item
          | ALTER TABLE table_ref DROP CONSTRAINT ID
          | ALTER SEQUENCE ID RESTART
          | ALTER INDEX ID RENAME TO ID;

/* Transaction */
begin_stmt: BEGIN_KW | BEGIN_KW TRANSACTION;
commit_stmt: COMMIT;
rollback_stmt: ROLLBACK | ROLLBACK TO SAVEPOINT ID;

/* SET / SHOW */
set_stmt: SET ID EQ expr | SET ID TO expr;
show_stmt: SHOW ID | SHOW ALL;

/* EXPLAIN */
explain_stmt: EXPLAIN select_stmt | EXPLAIN ANALYZE select_stmt
            | EXPLAIN VERBOSE select_stmt;

/* GRANT / REVOKE */
grant_stmt: GRANT privilege_list ON table_ref TO ID;
revoke_stmt: REVOKE privilege_list ON table_ref FROM ID;
privilege_list: privilege_list COMMA privilege | privilege;
privilege: SELECT | INSERT | UPDATE | DELETE | ALL;

/* PL/SQL style */
declare_stmt: DECLARE ID type_name | DECLARE ID type_name DEFAULT expr;
if_stmt: IF expr THEN_KW stmt_list END IF
       | IF expr THEN_KW stmt_list ELSE_KW stmt_list END IF
       | IF expr THEN_KW stmt_list ELSIF expr THEN_KW stmt_list END IF;
while_stmt: WHILE expr LOOP stmt_list END LOOP;
for_stmt: FOR ID IN expr DOTDOT expr LOOP stmt_list END LOOP;
return_stmt: RETURN expr | RETURN;
call_stmt: CALL ID LPAREN expr_list RPAREN | CALL ID LPAREN RPAREN;
raise_stmt: RAISE EXCEPTION STRING | RAISE NOTICE STRING;
block_stmt: BEGIN_KW stmt_list END;

/* WITH (CTE) */
with_stmt: WITH cte_list select_stmt | WITH RECURSIVE cte_list select_stmt;
cte_list: cte_list COMMA cte_def | cte_def;
cte_def: ID AS LPAREN select_stmt RPAREN;

/* Trigger */
trigger_timing: BEFORE | AFTER | INSTEAD OF;
trigger_event: INSERT | UPDATE | DELETE;

/* Aggregate */
aggregate_def_list: aggregate_def_list COMMA aggregate_def | aggregate_def;
aggregate_def: SFUNC EQ ID | STYPE EQ type_name | INITCOND EQ expr;

/* Params */
param_list: param_list COMMA param | param | /* empty */;
param: ID type_name | type_name;

/* Type */
type_name: ID | ID LPAREN INTEGER RPAREN | ID LPAREN INTEGER COMMA INTEGER RPAREN
         | ID LBRACKET RBRACKET | SETOF ID;

/* Expressions */
expr: expr PLUS expr | expr MINUS expr | expr STAR expr
    | expr SLASH expr | expr PERCENT expr
    | expr EQ expr | expr NE expr
    | expr LT expr | expr GT expr | expr LE expr | expr GE expr
    | expr AND expr | expr OR expr | NOT expr
    | MINUS expr %prec UMINUS
    | expr BETWEEN expr AND expr
    | expr NOT BETWEEN expr AND expr
    | expr IN LPAREN expr_list RPAREN
    | expr NOT IN LPAREN expr_list RPAREN
    | expr IN LPAREN select_stmt RPAREN
    | expr LIKE expr | expr NOT LIKE expr | expr ILIKE expr
    | expr IS NULL_KW | expr IS NOT NULL_KW
    | expr IS DISTINCT FROM expr
    | EXISTS LPAREN select_stmt RPAREN
    | CASE when_list else_opt END
    | CASE expr when_list else_opt END
    | CAST LPAREN expr AS type_name RPAREN
    | COALESCE LPAREN expr_list RPAREN
    | NULLIF LPAREN expr COMMA expr RPAREN
    | GREATEST LPAREN expr_list RPAREN
    | LEAST LPAREN expr_list RPAREN
    | ID LPAREN expr_list RPAREN | ID LPAREN STAR RPAREN
    | ID LPAREN RPAREN | ID LPAREN DISTINCT expr_list RPAREN
    | expr DOT ID | expr LBRACKET expr RBRACKET
    | expr LBRACKET expr COLON expr RBRACKET
    | LPAREN expr RPAREN | LPAREN select_stmt RPAREN
    | ID | INTEGER | FLOAT | STRING | NULL_KW | TRUE_KW | FALSE_KW | PARAM
    | ARRAY LBRACKET expr_list RBRACKET | ROW LPAREN expr_list RPAREN
    | expr COLLATE ID | expr TYPECAST type_name;

when_list: when_list WHEN expr THEN_KW expr | WHEN expr THEN_KW expr;
else_opt: ELSE_KW expr | /* empty */;

expr_list: expr_list COMMA expr | expr;
id_list: id_list COMMA ID | ID;
table_ref: ID | ID DOT ID | ID DOT ID DOT ID;

%%

/* yyerror / yylex are provided by the bench_runtime harness when this
** grammar is built into the bench_runtime_bison binary.  When this
** file is built standalone (e.g. for a bison sanity check), define
** -DBENCH_GRAMMAR_STANDALONE on the command line to get the no-op
** versions back. */
#ifdef BENCH_GRAMMAR_STANDALONE
void yyerror(const char *s) { (void)s; }
int yylex(void) { return 0; }
#endif

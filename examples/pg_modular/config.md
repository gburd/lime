# Parser Configuration


Core parser setup: name prefix, token type, includes, error
handlers, and start symbol declaration.


```yaml
name: pg-config
version: 17.0.0
description: Parser configuration, includes, error handlers, and start symbol
provides: [pg-config]
depends: [pg-tokens]
```

## Includes and Error Handlers

```lime parser-config
%include {
#include "postgres.h"
#include <ctype.h>
#include <limits.h>
#include "catalog/index.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_trigger.h"
#include "commands/defrem.h"
#include "commands/trigger.h"
#include "gramparse.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "parser/parser.h"
#include "utils/datetime.h"
#include "utils/xml.h"
#include "pg_gram_helpers.h"

#define LOC(tok) ((tok).location)
#define yyscanner (pstate->scanner)
}

%syntax_error { parser_yyerror("syntax error"); }
%parse_failure { parser_yyerror("parse failure"); }

%start_symbol parse_toplevel
```


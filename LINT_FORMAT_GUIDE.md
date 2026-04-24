# Lime Lint and Format Features

## Overview

Lime now includes built-in linting and formatting capabilities for grammar files.

---

## Lint Feature (`-L`)

### Usage

```bash
lime -L grammar.lime
```

### What It Checks

1. **Module Metadata Validation**:
   - `%module_name` requires `%module_version`
   - Version format must be valid semantic versioning (e.g., "1.0.0")

2. **Symbol Exports**:
   - Exported symbols must be defined in the grammar
   - Warnings if exporting terminals (usually non-terminals are exported)

3. **Unused Symbols**:
   - Detects non-terminals with type declarations but no production rules

4. **Grammar Structure**:
   - Validates symbol usage throughout rules

### Example Output

**Valid grammar:**
```bash
$ lime -L my_grammar.lime
Linting my_grammar.lime...

✓ No errors or warnings
```

**Grammar with issues:**
```bash
$ lime -L problem.lime
problem.lime:1:1: error: %module_name requires %module_version
problem.lime:1:1: error: exported symbol 'missing_rule' is not defined
problem.lime:1:1: warning: non-terminal 'unused_type' has type but no rules

2 errors, 1 warnings
Linting problem.lime...
```

### Exit Codes

- `0` - No errors (warnings OK)
- `1` - Errors found

---

## Format Feature (`-F`)

### Usage

```bash
lime -F grammar.lime
```

### What It Does

1. **Module Directives**: Groups and orders consistently
   - `%module_name`, `%module_version`, `%module_description`
   - `%require` statements
   - `%export` statements
   - `%import` statements (grouped by source module)

2. **Standard Directives**: Orders logically
   - `%name`, `%token_type`, `%extra_argument`, etc.

3. **Token Declarations**: Groups all `%token` statements

4. **Type Declarations**: Groups all `%type` statements

5. **Grammar Rules**: Formats consistently
   - One rule per line
   - Consistent spacing
   - Proper action block formatting

### Example

**Before:**
```lime
%module_name expr  %module_version "1.0.0"
%export stmt.
%token  INTEGER  PLUS.
stmt::=expr.
expr::=INTEGER|expr PLUS expr.
```

**After (`lime -F expr.lime`):**
```lime
/* Formatted by Lime */

%module_name expr
%module_version "1.0.0"

%export stmt.

%name ExprParser
%token_type {int}

/* Token declarations */
%token INTEGER.
%token PLUS.

/* Type declarations */
%type stmt {int}
%type expr {int}

/* Grammar rules */
stmt ::= expr.
expr ::= INTEGER.
expr ::= expr PLUS expr.
```

### Output

The formatted grammar is written to `grammar.lime.formatted`.

**Recommended workflow:**
```bash
# Format the file
lime -F my_grammar.lime

# Review the changes
diff my_grammar.lime my_grammar.lime.formatted

# If satisfied, replace the original
mv my_grammar.lime.formatted my_grammar.lime
```

---

## Typical Workflow

### Before Committing

```bash
# Lint your grammar
lime -L my_grammar.lime

# Format for consistency
lime -F my_grammar.lime
mv my_grammar.lime.formatted my_grammar.lime

# Generate parser
lime my_grammar.lime
```

### In CI/CD Pipeline

```bash
#!/bin/bash
# validate-grammar.sh

set -e

# Lint all grammar files
for f in *.lime; do
  echo "Linting $f..."
  lime -L "$f" || exit 1
done

echo "✓ All grammar files passed lint checks"
```

### Pre-commit Hook

```bash
#!/bin/bash
# .git/hooks/pre-commit

# Lint staged .lime files
for file in $(git diff --cached --name-only --diff-filter=ACM | grep '\.lime$'); do
  echo "Linting $file..."
  lime -L "$file" || {
    echo "Lint failed for $file"
    exit 1
  }
done
```

---

## Integration with Editors

### VSCode

Create `.vscode/tasks.json`:
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Lint Lime Grammar",
      "type": "shell",
      "command": "lime",
      "args": ["-L", "${file}"],
      "problemMatcher": []
    },
    {
      "label": "Format Lime Grammar",
      "type": "shell",
      "command": "lime",
      "args": ["-F", "${file}"],
      "problemMatcher": []
    }
  ]
}
```

### Vim

Add to `.vimrc`:
```vim
" Lint current Lime file
nnoremap <leader>ll :!lime -L %<CR>

" Format current Lime file
nnoremap <leader>lf :!lime -F % && mv %.formatted %<CR>:e<CR>
```

### Emacs

Add to init file:
```elisp
(defun lime-lint ()
  "Lint current Lime grammar file"
  (interactive)
  (compile (concat "lime -L " (buffer-file-name))))

(defun lime-format ()
  "Format current Lime grammar file"
  (interactive)
  (shell-command (concat "lime -F " (buffer-file-name)))
  (rename-file (concat (buffer-file-name) ".formatted")
               (buffer-file-name) t)
  (revert-buffer t t))
```

---

## Limitations

### Lint
- Does not validate C code in action blocks
- Does not check cross-module dependencies (use `lime-compose` for that)
- Basic symbol usage validation (can be extended)

### Format
- Preserves C code blocks as-is (no C formatting)
- Does not reorder rules (preserves original order)
- Creates new file instead of modifying in-place (safety)

---

## Future Enhancements

Planned improvements:
- Configurable formatting options (indent size, alignment, etc.)
- More sophisticated lint rules (reachability analysis, etc.)
- Integration with language servers (LSP)
- Auto-fix capabilities for common issues

---

## Examples

See the examples directory for sample grammars:
- `examples/pg_modular/` - Large modular grammar
- `examples/datalog/` - Simple grammar with modules

---

## Getting Help

For issues or feature requests:
- Documentation: `docs/MODULE_FORMAT.md`
- GitHub Issues: https://github.com/anthropics/lime/issues

---

**Added in Lime v2.0 (2026-04-24)**

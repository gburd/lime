;;; lime-mode.el --- Major mode for Lime parser-generator grammar files -*- lexical-binding: t -*-
;;
;; Copyright (c) 2024-2026 Lime Parser Generator Project
;; License: Public Domain
;;
;; Provides syntax highlighting and basic editing support for Lime
;; grammar files (.lime, .y, .lex when used with Lime's -X flag).
;;
;; Features:
;;   - font-lock-mode highlighting for directives, terminals,
;;     non-terminals, operators, and embedded C action blocks
;;   - Comment syntax (/* ... */ block, // line)
;;   - Auto-mode-alist hookup for .lime
;;   - imenu support: jump to rule definitions and directives
;;
;; Installation
;;   (add-to-list 'load-path "/path/to/lime/editors")
;;   (require 'lime-mode)
;;
;; Or copy this file into your site-lisp / use-package recipe.
;;
;;; Code:

(defgroup lime nil
  "Major mode for editing Lime parser grammar files."
  :group 'languages
  :prefix "lime-")

(defvar lime-mode-syntax-table
  (let ((st (make-syntax-table)))
    ;; C-style block comments: /* ... */
    (modify-syntax-entry ?/ ". 124b" st)
    (modify-syntax-entry ?* ". 23"   st)
    ;; Line comments: // ...
    (modify-syntax-entry ?\n "> b"   st)
    ;; Strings: "..." and '...'
    (modify-syntax-entry ?\" "\""    st)
    (modify-syntax-entry ?\' "\""    st)
    ;; Identifiers can include underscores and digits
    (modify-syntax-entry ?_ "w"      st)
    ;; Curly braces and parens are punctuation, not delimiters
    ;; (action blocks are handled by font-lock, not the syntax table)
    st)
  "Syntax table for `lime-mode'.")

;; Directives recognised by lime.c.  Sourced by:
;;   grep -oE 'strcmp\\(.*x.*, *"[a-z_]+"' lime.c | sort -u
;; Keep alphabetical so additions land in the right place.
(defconst lime-mode-directives
  '("ast_auto" "ast_list" "ast_node" "ast_prefix"
    "code" "default_destructor" "default_type" "destructor"
    "error_sync" "expect" "export"
    "extra_argument" "extra_context"
    "fallback" "first_token" "free" "from"
    "ifdef" "ifndef" "else" "endif"
    "import" "include"
    "left" "location_type" "locations"
    "module_description" "module_name" "module_version"
    "name" "name_prefix" "nonassoc"
    "parse_accept" "parse_failure"
    "realloc" "require" "right"
    "stack_overflow" "stack_size" "stack_size_limit"
    "start" "start_symbol" "symbol_prefix" "syntax_error"
    "token" "token_class" "token_destructor" "token_prefix" "token_type"
    "type" "wildcard")
  "All %-directives recognised by the Lime grammar generator.")

(defconst lime-mode-directive-regexp
  (concat "%"
          (regexp-opt lime-mode-directives 'symbols))
  "Regexp matching any Lime directive.")

(defconst lime-mode-rule-arrow-regexp
  "::="
  "The rule production arrow.")

(defconst lime-mode-rule-lhs-regexp
  "^[ \t]*\\([a-zA-Z_][a-zA-Z0-9_]*\\)\\(?:\\s-*([A-Za-z]+)\\)?\\s-*::="
  "Match the LHS of a grammar rule (optionally with alias).")

(defconst lime-mode-token-name-regexp
  "\\b[A-Z_][A-Z0-9_]*\\b"
  "All-uppercase identifiers are conventionally token codes.")

(defconst lime-mode-precedence-marker-regexp
  "\\[[A-Z_][A-Z0-9_]*\\]"
  "Precedence markers like [UMINUS] used to override rule precedence.")

(defvar lime-mode-font-lock-keywords
  `(
    ;; Comments are handled by syntax table, not font-lock.

    ;; Directives: %name, %token, %type, %include, %left, etc.
    (,lime-mode-directive-regexp . font-lock-preprocessor-face)

    ;; Rule arrow ::=
    (,lime-mode-rule-arrow-regexp . font-lock-keyword-face)

    ;; LHS of a rule (the non-terminal being defined)
    (,lime-mode-rule-lhs-regexp 1 font-lock-function-name-face)

    ;; Token names (all-caps identifiers)
    (,lime-mode-token-name-regexp . font-lock-constant-face)

    ;; Precedence markers
    (,lime-mode-precedence-marker-regexp . font-lock-builtin-face)

    ;; Aliases like (A) (B) attached to grammar symbols
    ("(\\([A-Za-z][A-Za-z0-9_]*\\))" 1 font-lock-variable-name-face)
    )
  "Default `font-lock-keywords' for `lime-mode'.")

(defun lime-mode--imenu-create ()
  "Build an imenu index of rule LHS names."
  (let (entries)
    (save-excursion
      (goto-char (point-min))
      (while (re-search-forward lime-mode-rule-lhs-regexp nil t)
        (push (cons (match-string 1) (match-beginning 1)) entries)))
    (nreverse entries)))

;;;###autoload
(define-derived-mode lime-mode prog-mode "Lime"
  "Major mode for editing Lime parser-generator grammar files."
  :syntax-table lime-mode-syntax-table
  (setq-local font-lock-defaults '(lime-mode-font-lock-keywords nil nil))
  (setq-local comment-start "// ")
  (setq-local comment-end "")
  (setq-local comment-start-skip "\\(?://+\\|/\\*+\\)\\s *")
  (setq-local indent-tabs-mode nil)
  (setq-local tab-width 4)
  (setq-local imenu-create-index-function #'lime-mode--imenu-create))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.lime\\'" . lime-mode))

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.lex\\'" . lime-mode))

(provide 'lime-mode)
;;; lime-mode.el ends here

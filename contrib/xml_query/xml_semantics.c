/*
** XML Query Semantic Actions
**
** Provides the semantic action implementations for the XQuery and XPath
** grammars when embedded in SQL via xmlquery() and xpath() functions.
**
** The semantic layer bridges between the SQL parser's value system and
** the XML query AST representation.  It handles:
**   - Variable binding from SQL PASSING clause to XQuery variables
**   - Result coercion from XQuery/XPath results to SQL types
**   - Error propagation from XML evaluation to SQL error reporting
*/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/*  XML value types                                                    */
/* ------------------------------------------------------------------ */

typedef enum XmlValueType {
    XML_VAL_NULL = 0,
    XML_VAL_STRING,
    XML_VAL_INTEGER,
    XML_VAL_DECIMAL,
    XML_VAL_BOOLEAN,
    XML_VAL_NODE,
    XML_VAL_NODESET,
    XML_VAL_DOCUMENT,
    XML_VAL_SEQUENCE,
} XmlValueType;

typedef struct XmlValue {
    XmlValueType type;
    union {
        char *string;
        int64_t integer;
        double decimal;
        bool boolean;
        void *node;       /* Opaque pointer to XML DOM node */
    } u;
    struct XmlValue *next;  /* For sequences / node sets */
} XmlValue;

/* ------------------------------------------------------------------ */
/*  XML value constructors                                             */
/* ------------------------------------------------------------------ */

XmlValue *xml_value_null(void) {
    XmlValue *v = calloc(1, sizeof(XmlValue));
    if (v) v->type = XML_VAL_NULL;
    return v;
}

XmlValue *xml_value_string(const char *s) {
    XmlValue *v = calloc(1, sizeof(XmlValue));
    if (v == NULL) return NULL;
    v->type = XML_VAL_STRING;
    v->u.string = s ? strdup(s) : NULL;
    return v;
}

XmlValue *xml_value_integer(int64_t i) {
    XmlValue *v = calloc(1, sizeof(XmlValue));
    if (v == NULL) return NULL;
    v->type = XML_VAL_INTEGER;
    v->u.integer = i;
    return v;
}

XmlValue *xml_value_decimal(double d) {
    XmlValue *v = calloc(1, sizeof(XmlValue));
    if (v == NULL) return NULL;
    v->type = XML_VAL_DECIMAL;
    v->u.decimal = d;
    return v;
}

XmlValue *xml_value_boolean(bool b) {
    XmlValue *v = calloc(1, sizeof(XmlValue));
    if (v == NULL) return NULL;
    v->type = XML_VAL_BOOLEAN;
    v->u.boolean = b;
    return v;
}

void xml_value_free(XmlValue *v) {
    while (v != NULL) {
        XmlValue *next = v->next;
        if (v->type == XML_VAL_STRING) {
            free(v->u.string);
        }
        free(v);
        v = next;
    }
}

/* ------------------------------------------------------------------ */
/*  Variable bindings (SQL PASSING clause)                             */
/* ------------------------------------------------------------------ */

typedef struct XmlVarBinding {
    char *name;               /* Variable name (without $) */
    XmlValue *value;          /* Bound value */
    struct XmlVarBinding *next;
} XmlVarBinding;

typedef struct XmlBindingContext {
    XmlVarBinding *bindings;  /* Linked list of bindings */
    uint32_t count;
} XmlBindingContext;

XmlBindingContext *xml_binding_create(void) {
    return calloc(1, sizeof(XmlBindingContext));
}

void xml_binding_destroy(XmlBindingContext *ctx) {
    if (ctx == NULL) return;
    XmlVarBinding *b = ctx->bindings;
    while (b != NULL) {
        XmlVarBinding *next = b->next;
        free(b->name);
        xml_value_free(b->value);
        free(b);
        b = next;
    }
    free(ctx);
}

bool xml_binding_add(XmlBindingContext *ctx, const char *name, XmlValue *value) {
    if (ctx == NULL || name == NULL) return false;

    XmlVarBinding *b = calloc(1, sizeof(XmlVarBinding));
    if (b == NULL) return false;

    b->name = strdup(name);
    if (b->name == NULL) {
        free(b);
        return false;
    }
    b->value = value;
    b->next = ctx->bindings;
    ctx->bindings = b;
    ctx->count++;
    return true;
}

const XmlValue *xml_binding_lookup(const XmlBindingContext *ctx, const char *name) {
    if (ctx == NULL || name == NULL) return NULL;
    for (const XmlVarBinding *b = ctx->bindings; b != NULL; b = b->next) {
        if (strcmp(b->name, name) == 0) {
            return b->value;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Result coercion (XQuery/XPath result -> SQL value)                 */
/* ------------------------------------------------------------------ */

typedef enum SqlCoercionType {
    SQL_COERCE_VARCHAR,
    SQL_COERCE_INTEGER,
    SQL_COERCE_FLOAT,
    SQL_COERCE_BOOLEAN,
    SQL_COERCE_XML,
} SqlCoercionType;

/*
** Coerce an XML value to a SQL-compatible string representation.
** Returns a malloc'd string that the caller must free.
*/
char *xml_coerce_to_string(const XmlValue *val) {
    if (val == NULL) return strdup("NULL");

    switch (val->type) {
        case XML_VAL_NULL:
            return strdup("NULL");
        case XML_VAL_STRING:
            return val->u.string ? strdup(val->u.string) : strdup("");
        case XML_VAL_INTEGER: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", (long)val->u.integer);
            return strdup(buf);
        }
        case XML_VAL_DECIMAL: {
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", val->u.decimal);
            return strdup(buf);
        }
        case XML_VAL_BOOLEAN:
            return strdup(val->u.boolean ? "true" : "false");
        case XML_VAL_NODE:
        case XML_VAL_DOCUMENT:
            return strdup("<xml-node>");
        case XML_VAL_NODESET:
        case XML_VAL_SEQUENCE: {
            /* Concatenate string representations of sequence members */
            size_t total = 0;
            for (const XmlValue *v = val; v != NULL; v = v->next) {
                total += 32;  /* rough estimate per item */
            }
            char *result = calloc(total + 1, 1);
            if (result == NULL) return strdup("");
            size_t offset = 0;
            for (const XmlValue *v = val; v != NULL; v = v->next) {
                char *item = xml_coerce_to_string(v);
                if (item) {
                    size_t ilen = strlen(item);
                    if (offset + ilen + 2 < total) {
                        if (offset > 0) result[offset++] = ' ';
                        memcpy(result + offset, item, ilen);
                        offset += ilen;
                    }
                    free(item);
                }
            }
            result[offset] = '\0';
            return result;
        }
    }
    return strdup("NULL");
}

/*
** Coerce an XML value to an integer.
** Returns true on success.
*/
bool xml_coerce_to_integer(const XmlValue *val, int64_t *out) {
    if (val == NULL || out == NULL) return false;

    switch (val->type) {
        case XML_VAL_INTEGER:
            *out = val->u.integer;
            return true;
        case XML_VAL_DECIMAL:
            *out = (int64_t)val->u.decimal;
            return true;
        case XML_VAL_BOOLEAN:
            *out = val->u.boolean ? 1 : 0;
            return true;
        case XML_VAL_STRING:
            if (val->u.string) {
                char *end;
                long long v = strtoll(val->u.string, &end, 10);
                if (end != val->u.string && *end == '\0') {
                    *out = (int64_t)v;
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

/* ------------------------------------------------------------------ */
/*  Error reporting                                                    */
/* ------------------------------------------------------------------ */

typedef struct XmlQueryError {
    int code;
    char *message;
    int line;
    int column;
} XmlQueryError;

XmlQueryError *xml_error_create(int code, const char *fmt, ...) {
    XmlQueryError *err = calloc(1, sizeof(XmlQueryError));
    if (err == NULL) return NULL;
    err->code = code;

    /* Simple message copy (no va_args for portability) */
    err->message = fmt ? strdup(fmt) : strdup("unknown error");
    return err;
}

void xml_error_destroy(XmlQueryError *err) {
    if (err == NULL) return;
    free(err->message);
    free(err);
}

/* ------------------------------------------------------------------ */
/*  Integration helpers: SQL xmlquery() / xpath() function stubs       */
/* ------------------------------------------------------------------ */

/*
** Evaluate an xmlquery() expression.
** This is a stub that demonstrates the integration pattern between
** the SQL parser, grammar context switching, and the XQuery evaluator.
**
** In a real implementation:
**   1. SQL parser encounters xmlquery(...)
**   2. Grammar context switches to MODE_XQUERY
**   3. XQuery expression is parsed into an AST
**   4. Grammar context switches back to MODE_SQL
**   5. PASSING clause binds SQL values to XQuery variables
**   6. XQuery AST is evaluated against the XML document
**   7. Result is coerced to the SQL return type
*/
XmlValue *xmlquery_evaluate(const char *xquery_text,
                            const XmlValue *xml_document,
                            const XmlBindingContext *bindings,
                            XmlQueryError **error_out) {
    (void)xml_document;
    (void)bindings;

    if (xquery_text == NULL) {
        if (error_out) {
            *error_out = xml_error_create(1, "NULL XQuery expression");
        }
        return xml_value_null();
    }

    /* Stub: return a string describing what would be evaluated */
    char buf[256];
    snprintf(buf, sizeof(buf), "[xmlquery result: %s]", xquery_text);
    return xml_value_string(buf);
}

/*
** Evaluate an xpath() expression.
** Stub implementation following the same pattern as xmlquery_evaluate.
*/
XmlValue *xpath_evaluate(const char *xpath_text,
                         const XmlValue *xml_document,
                         XmlQueryError **error_out) {
    (void)xml_document;

    if (xpath_text == NULL) {
        if (error_out) {
            *error_out = xml_error_create(1, "NULL XPath expression");
        }
        return xml_value_null();
    }

    /* Stub: return a string describing what would be evaluated */
    char buf[256];
    snprintf(buf, sizeof(buf), "[xpath result: %s]", xpath_text);
    return xml_value_string(buf);
}

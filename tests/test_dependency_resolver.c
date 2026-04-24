/*
** Unit tests for the dependency resolver.
**
** Tests semantic versioning, dependency graph construction, topological
** sorting, circular dependency detection, version constraint validation,
** and symbol import/export composition checks.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dependency_resolver.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-60s ", name); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* ------------------------------------------------------------------ */
/*  Test helpers                                                       */
/* ------------------------------------------------------------------ */

static ParserModule *make_module(const char *name,
                                 uint32_t major, uint32_t minor,
                                 uint32_t patch) {
    ParserModule *m = calloc(1, sizeof(ParserModule));
    if (m == NULL) return NULL;
    m->name = strdup(name);
    m->version.major = major;
    m->version.minor = minor;
    m->version.patch = patch;
    m->version.prerelease = NULL;
    return m;
}

static void free_module(ParserModule *m) {
    if (m == NULL) return;
    parser_module_destroy_contents(m);
    free(m);
}

static bool add_dependency(ParserModule *mod, const char *dep_name,
                           bool optional,
                           VersionOp op, uint32_t maj, uint32_t min,
                           uint32_t pat) {
    uint32_t idx = mod->ndependencies;
    ParserDependency *new_deps = realloc(mod->dependencies,
        (idx + 1) * sizeof(ParserDependency));
    if (new_deps == NULL) return false;
    mod->dependencies = new_deps;

    ParserDependency *dep = &mod->dependencies[idx];
    memset(dep, 0, sizeof(*dep));
    dep->module_name = strdup(dep_name);
    dep->optional = optional;

    if (maj != UINT32_MAX) {
        dep->constraints = malloc(sizeof(VersionConstraint));
        if (dep->constraints == NULL) return false;
        dep->nconstraints = 1;
        dep->constraints[0].op = op;
        dep->constraints[0].version.major = maj;
        dep->constraints[0].version.minor = min;
        dep->constraints[0].version.patch = pat;
        dep->constraints[0].version.prerelease = NULL;
    }

    mod->ndependencies = idx + 1;
    return true;
}

static bool add_export(ParserModule *mod, const char *sym) {
    char **new_exp = realloc(mod->exports,
        (mod->nexports + 1) * sizeof(char *));
    if (new_exp == NULL) return false;
    mod->exports = new_exp;
    mod->exports[mod->nexports++] = strdup(sym);
    return true;
}

static bool add_import(ParserModule *mod, const char *sym) {
    char **new_imp = realloc(mod->imports,
        (mod->nimports + 1) * sizeof(char *));
    if (new_imp == NULL) return false;
    mod->imports = new_imp;
    mod->imports[mod->nimports++] = strdup(sym);
    return true;
}

/* ================================================================== */
/*  SemVer tests                                                      */
/* ================================================================== */

static void test_semver_parse_basic(void) {
    TEST("semver_parse basic '1.2.3'");
    SemVer v;
    if (!semver_parse("1.2.3", &v)) { FAIL("parse failed"); return; }
    if (v.major != 1 || v.minor != 2 || v.patch != 3) {
        FAIL("wrong values"); semver_destroy(&v); return;
    }
    if (v.prerelease != NULL) {
        FAIL("unexpected prerelease"); semver_destroy(&v); return;
    }
    semver_destroy(&v);
    PASS();
}

static void test_semver_parse_prerelease(void) {
    TEST("semver_parse with prerelease '1.0.0-alpha.1'");
    SemVer v;
    if (!semver_parse("1.0.0-alpha.1", &v)) { FAIL("parse failed"); return; }
    if (v.major != 1 || v.minor != 0 || v.patch != 0) {
        FAIL("wrong values"); semver_destroy(&v); return;
    }
    if (v.prerelease == NULL || strcmp(v.prerelease, "alpha.1") != 0) {
        FAIL("wrong prerelease"); semver_destroy(&v); return;
    }
    semver_destroy(&v);
    PASS();
}

static void test_semver_parse_zero(void) {
    TEST("semver_parse '0.0.0'");
    SemVer v;
    if (!semver_parse("0.0.0", &v)) { FAIL("parse failed"); return; }
    if (v.major != 0 || v.minor != 0 || v.patch != 0) {
        FAIL("wrong values"); semver_destroy(&v); return;
    }
    semver_destroy(&v);
    PASS();
}

static void test_semver_parse_invalid(void) {
    TEST("semver_parse rejects invalid strings");
    SemVer v;
    if (semver_parse(NULL, &v))   { FAIL("accepted NULL");  return; }
    if (semver_parse("", &v))     { FAIL("accepted empty"); return; }
    if (semver_parse("abc", &v))  { FAIL("accepted 'abc'"); return; }
    if (semver_parse("1.2", &v))  {
        /* "1.2" is accepted as 1.2.0 -- that's fine for flexibility */
        semver_destroy(&v);
    }
    PASS();
}

static void test_semver_compare_basic(void) {
    TEST("semver_compare ordering");
    SemVer a, b;
    semver_parse("1.0.0", &a);
    semver_parse("2.0.0", &b);
    if (semver_compare(&a, &b) >= 0) { FAIL("1.0.0 should be < 2.0.0"); goto cleanup; }

    semver_destroy(&a); semver_destroy(&b);
    semver_parse("1.2.0", &a);
    semver_parse("1.3.0", &b);
    if (semver_compare(&a, &b) >= 0) { FAIL("1.2.0 should be < 1.3.0"); goto cleanup; }

    semver_destroy(&a); semver_destroy(&b);
    semver_parse("1.2.3", &a);
    semver_parse("1.2.4", &b);
    if (semver_compare(&a, &b) >= 0) { FAIL("1.2.3 should be < 1.2.4"); goto cleanup; }

    semver_destroy(&a); semver_destroy(&b);
    semver_parse("1.2.3", &a);
    semver_parse("1.2.3", &b);
    if (semver_compare(&a, &b) != 0) { FAIL("1.2.3 should == 1.2.3"); goto cleanup; }

    semver_destroy(&a); semver_destroy(&b);
    PASS();
    return;
cleanup:
    semver_destroy(&a); semver_destroy(&b);
}

static void test_semver_compare_prerelease(void) {
    TEST("semver_compare prerelease < release");
    SemVer a, b;
    semver_parse("1.0.0-alpha", &a);
    semver_parse("1.0.0", &b);
    if (semver_compare(&a, &b) >= 0) {
        FAIL("1.0.0-alpha should be < 1.0.0");
        semver_destroy(&a); semver_destroy(&b);
        return;
    }
    semver_destroy(&a); semver_destroy(&b);
    PASS();
}

static void test_semver_satisfies_gte(void) {
    TEST("semver_satisfies >= constraint");
    SemVer v;
    semver_parse("1.5.0", &v);
    VersionConstraint c = { .op = VERSION_OP_GTE };
    semver_parse("1.0.0", &c.version);

    if (!semver_satisfies(&v, &c)) { FAIL("1.5.0 should satisfy >=1.0.0"); }
    else { PASS(); }
    semver_destroy(&v);
    semver_destroy(&c.version);
}

static void test_semver_satisfies_lt(void) {
    TEST("semver_satisfies < constraint");
    SemVer v;
    semver_parse("0.9.0", &v);
    VersionConstraint c = { .op = VERSION_OP_LT };
    semver_parse("1.0.0", &c.version);

    if (!semver_satisfies(&v, &c)) { FAIL("0.9.0 should satisfy <1.0.0"); }
    else { PASS(); }
    semver_destroy(&v);
    semver_destroy(&c.version);
}

static void test_semver_satisfies_caret(void) {
    TEST("semver_satisfies ^ (caret) constraint");
    SemVer v;
    VersionConstraint c = { .op = VERSION_OP_CARET };
    semver_parse("1.2.0", &c.version);

    /* 1.9.9 should satisfy ^1.2.0 (same major, >= 1.2.0) */
    semver_parse("1.9.9", &v);
    if (!semver_satisfies(&v, &c)) {
        FAIL("1.9.9 should satisfy ^1.2.0");
        semver_destroy(&v); semver_destroy(&c.version);
        return;
    }
    semver_destroy(&v);

    /* 2.0.0 should NOT satisfy ^1.2.0 */
    semver_parse("2.0.0", &v);
    if (semver_satisfies(&v, &c)) {
        FAIL("2.0.0 should not satisfy ^1.2.0");
        semver_destroy(&v); semver_destroy(&c.version);
        return;
    }
    semver_destroy(&v);

    /* 1.1.0 should NOT satisfy ^1.2.0 (less than) */
    semver_parse("1.1.0", &v);
    if (semver_satisfies(&v, &c)) {
        FAIL("1.1.0 should not satisfy ^1.2.0");
        semver_destroy(&v); semver_destroy(&c.version);
        return;
    }
    semver_destroy(&v);
    semver_destroy(&c.version);
    PASS();
}

static void test_semver_satisfies_tilde(void) {
    TEST("semver_satisfies ~ (tilde) constraint");
    SemVer v;
    VersionConstraint c = { .op = VERSION_OP_TILDE };
    semver_parse("1.2.0", &c.version);

    /* 1.2.9 should satisfy ~1.2.0 (same major.minor, >= 1.2.0) */
    semver_parse("1.2.9", &v);
    if (!semver_satisfies(&v, &c)) {
        FAIL("1.2.9 should satisfy ~1.2.0");
        semver_destroy(&v); semver_destroy(&c.version);
        return;
    }
    semver_destroy(&v);

    /* 1.3.0 should NOT satisfy ~1.2.0 */
    semver_parse("1.3.0", &v);
    if (semver_satisfies(&v, &c)) {
        FAIL("1.3.0 should not satisfy ~1.2.0");
        semver_destroy(&v); semver_destroy(&c.version);
        return;
    }
    semver_destroy(&v);
    semver_destroy(&c.version);
    PASS();
}

static void test_semver_satisfies_caret_zero_major(void) {
    TEST("semver_satisfies ^0.2.3 (zero major)");
    VersionConstraint c = { .op = VERSION_OP_CARET };
    semver_parse("0.2.3", &c.version);

    /* ^0.2.3 means >=0.2.3, <0.3.0 */
    SemVer v;
    semver_parse("0.2.5", &v);
    if (!semver_satisfies(&v, &c)) {
        FAIL("0.2.5 should satisfy ^0.2.3");
        semver_destroy(&v); semver_destroy(&c.version);
        return;
    }
    semver_destroy(&v);

    semver_parse("0.3.0", &v);
    if (semver_satisfies(&v, &c)) {
        FAIL("0.3.0 should not satisfy ^0.2.3");
        semver_destroy(&v); semver_destroy(&c.version);
        return;
    }
    semver_destroy(&v);
    semver_destroy(&c.version);
    PASS();
}

/* ================================================================== */
/*  Dependency graph tests                                            */
/* ================================================================== */

static void test_graph_create_destroy(void) {
    TEST("dep_graph_create and destroy");
    DependencyGraph *g = dep_graph_create();
    if (g == NULL) { FAIL("create returned NULL"); return; }
    dep_graph_destroy(g);
    PASS();
}

static void test_graph_empty(void) {
    TEST("resolve empty graph");
    DependencyGraph *g = dep_graph_create();
    ParserModule **mods = NULL;
    DepError err = {0};
    DepResolveResult rc = build_dependency_graph(mods, 0, g, &err);
    if (rc != DEP_OK) {
        FAIL("build failed on empty graph");
        dep_error_destroy(&err);
        dep_graph_destroy(g);
        return;
    }

    uint32_t *order = NULL;
    uint32_t norder = 0;
    rc = resolve_dependencies(g, &order, &norder, &err);
    if (rc != DEP_OK) {
        FAIL("resolve failed on empty graph");
        dep_error_destroy(&err);
        dep_graph_destroy(g);
        return;
    }
    if (norder != 0) {
        FAIL("expected 0 modules in order");
        free(order);
        dep_graph_destroy(g);
        return;
    }
    free(order);
    dep_graph_destroy(g);
    PASS();
}

static void test_graph_single_module(void) {
    TEST("resolve single module (no deps)");
    ParserModule *m = make_module("core", 1, 0, 0);
    ParserModule *mods[] = { m };

    DependencyGraph *g = dep_graph_create();
    DepError err = {0};
    DepResolveResult rc = build_dependency_graph(mods, 1, g, &err);
    if (rc != DEP_OK) {
        FAIL("build failed"); goto cleanup1;
    }

    uint32_t *order = NULL;
    uint32_t norder = 0;
    rc = resolve_dependencies(g, &order, &norder, &err);
    if (rc != DEP_OK) {
        FAIL("resolve failed"); goto cleanup1;
    }
    if (norder != 1 || order[0] != 0) {
        FAIL("wrong order"); free(order); goto cleanup1;
    }
    free(order);
    PASS();
cleanup1:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(m);
}

static void test_graph_linear_chain(void) {
    TEST("resolve linear chain A -> B -> C");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 1, 0, 0);
    ParserModule *c = make_module("C", 1, 0, 0);

    /* A depends on B, B depends on C */
    add_dependency(a, "B", false, VERSION_OP_GTE, 1, 0, 0);
    add_dependency(b, "C", false, VERSION_OP_GTE, 1, 0, 0);

    ParserModule *mods[] = { a, b, c };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 3, g, &err);
    if (rc != DEP_OK) {
        FAIL("build failed"); goto cleanup2;
    }

    uint32_t *order = NULL;
    uint32_t norder = 0;
    rc = resolve_dependencies(g, &order, &norder, &err);
    if (rc != DEP_OK) {
        FAIL("resolve failed"); goto cleanup2;
    }
    if (norder != 3) {
        FAIL("expected 3 modules"); free(order); goto cleanup2;
    }

    /* C must come first, then B, then A */
    if (order[0] != 2 || order[1] != 1 || order[2] != 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "wrong order: %u,%u,%u (expected 2,1,0)",
                 order[0], order[1], order[2]);
        FAIL(buf);
        free(order); goto cleanup2;
    }

    free(order);
    PASS();
cleanup2:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b); free_module(c);
}

static void test_graph_diamond(void) {
    TEST("resolve diamond: A -> B,C -> D");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 1, 0, 0);
    ParserModule *c = make_module("C", 1, 0, 0);
    ParserModule *d = make_module("D", 1, 0, 0);

    /* A depends on B and C; B and C both depend on D */
    add_dependency(a, "B", false, VERSION_OP_GTE, 1, 0, 0);
    add_dependency(a, "C", false, VERSION_OP_GTE, 1, 0, 0);
    add_dependency(b, "D", false, VERSION_OP_GTE, 1, 0, 0);
    add_dependency(c, "D", false, VERSION_OP_GTE, 1, 0, 0);

    ParserModule *mods[] = { a, b, c, d };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 4, g, &err);
    if (rc != DEP_OK) {
        FAIL("build failed"); goto cleanup3;
    }

    uint32_t *order = NULL;
    uint32_t norder = 0;
    rc = resolve_dependencies(g, &order, &norder, &err);
    if (rc != DEP_OK) {
        FAIL("resolve failed"); goto cleanup3;
    }
    if (norder != 4) {
        FAIL("expected 4 modules"); free(order); goto cleanup3;
    }

    /* D must come before B and C; B and C must come before A.
    ** A is index 0, B is 1, C is 2, D is 3.
    ** So order[0] must be D(3), order[3] must be A(0),
    ** and B(1)/C(2) can be in either middle position. */
    if (order[0] != 3) {
        char buf[64];
        snprintf(buf, sizeof(buf), "D should be first, got index %u", order[0]);
        FAIL(buf); free(order); goto cleanup3;
    }
    if (order[3] != 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "A should be last, got index %u", order[3]);
        FAIL(buf); free(order); goto cleanup3;
    }

    free(order);
    PASS();
cleanup3:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b); free_module(c); free_module(d);
}

/* ================================================================== */
/*  Circular dependency tests                                         */
/* ================================================================== */

static void test_circular_two_nodes(void) {
    TEST("detect circular dependency A <-> B");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 1, 0, 0);

    add_dependency(a, "B", false, VERSION_OP_GTE, 1, 0, 0);
    add_dependency(b, "A", false, VERSION_OP_GTE, 1, 0, 0);

    ParserModule *mods[] = { a, b };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 2, g, &err);
    if (rc != DEP_OK) {
        FAIL("build failed"); goto cleanup4;
    }

    char *cycle = NULL;
    if (!has_circular_dependencies(g, &cycle)) {
        FAIL("should detect circular dependency");
        goto cleanup4;
    }
    if (cycle == NULL) {
        FAIL("cycle path should not be NULL");
        goto cleanup4;
    }
    /* Cycle path should mention both A and B */
    if (strstr(cycle, "A") == NULL || strstr(cycle, "B") == NULL) {
        char buf[256];
        snprintf(buf, sizeof(buf), "cycle path missing module names: '%s'", cycle);
        FAIL(buf);
        free(cycle); goto cleanup4;
    }
    free(cycle);

    /* resolve_dependencies should also fail */
    uint32_t *order = NULL;
    uint32_t norder = 0;
    rc = resolve_dependencies(g, &order, &norder, &err);
    if (rc != DEP_ERR_CIRCULAR) {
        FAIL("resolve should return DEP_ERR_CIRCULAR");
        free(order); goto cleanup4;
    }

    PASS();
cleanup4:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b);
}

static void test_circular_three_nodes(void) {
    TEST("detect circular dependency A -> B -> C -> A");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 1, 0, 0);
    ParserModule *c = make_module("C", 1, 0, 0);

    add_dependency(a, "B", false, VERSION_OP_GTE, 1, 0, 0);
    add_dependency(b, "C", false, VERSION_OP_GTE, 1, 0, 0);
    add_dependency(c, "A", false, VERSION_OP_GTE, 1, 0, 0);

    ParserModule *mods[] = { a, b, c };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 3, g, &err);
    if (rc != DEP_OK) { FAIL("build failed"); goto cleanup5; }

    char *cycle = NULL;
    if (!has_circular_dependencies(g, &cycle)) {
        FAIL("should detect 3-node cycle");
        goto cleanup5;
    }
    free(cycle);
    PASS();
cleanup5:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b); free_module(c);
}

static void test_no_cycle_in_dag(void) {
    TEST("no false positive cycle in valid DAG");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 1, 0, 0);
    ParserModule *c = make_module("C", 1, 0, 0);

    add_dependency(a, "B", false, VERSION_OP_GTE, 1, 0, 0);
    add_dependency(a, "C", false, VERSION_OP_GTE, 1, 0, 0);
    add_dependency(b, "C", false, VERSION_OP_GTE, 1, 0, 0);

    ParserModule *mods[] = { a, b, c };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 3, g, &err);
    if (rc != DEP_OK) { FAIL("build failed"); goto cleanup6; }

    if (has_circular_dependencies(g, NULL)) {
        FAIL("false positive: no cycle exists");
        goto cleanup6;
    }
    PASS();
cleanup6:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b); free_module(c);
}

/* ================================================================== */
/*  Missing dependency tests                                          */
/* ================================================================== */

static void test_missing_required_dep(void) {
    TEST("missing required dependency produces error");
    ParserModule *a = make_module("A", 1, 0, 0);
    add_dependency(a, "nonexistent", false, VERSION_OP_GTE, 1, 0, 0);

    ParserModule *mods[] = { a };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 1, g, &err);
    if (rc != DEP_ERR_MISSING_DEP) {
        FAIL("expected DEP_ERR_MISSING_DEP");
        goto cleanup7;
    }
    if (err.message == NULL || strstr(err.message, "nonexistent") == NULL) {
        FAIL("error message should mention the missing module");
        goto cleanup7;
    }
    PASS();
cleanup7:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a);
}

static void test_missing_optional_dep(void) {
    TEST("missing optional dependency is silently skipped");
    ParserModule *a = make_module("A", 1, 0, 0);
    add_dependency(a, "optional_mod", true, VERSION_OP_GTE, 1, 0, 0);

    ParserModule *mods[] = { a };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 1, g, &err);
    if (rc != DEP_OK) {
        FAIL("build should succeed with missing optional dep");
        goto cleanup8;
    }

    uint32_t *order = NULL;
    uint32_t norder = 0;
    rc = resolve_dependencies(g, &order, &norder, &err);
    if (rc != DEP_OK || norder != 1) {
        FAIL("resolve should succeed"); free(order); goto cleanup8;
    }
    free(order);
    PASS();
cleanup8:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a);
}

/* ================================================================== */
/*  Duplicate module name test                                        */
/* ================================================================== */

static void test_duplicate_module_name(void) {
    TEST("duplicate module name produces error");
    ParserModule *a1 = make_module("A", 1, 0, 0);
    ParserModule *a2 = make_module("A", 2, 0, 0);

    ParserModule *mods[] = { a1, a2 };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 2, g, &err);
    if (rc != DEP_ERR_DUPLICATE_MODULE) {
        FAIL("expected DEP_ERR_DUPLICATE_MODULE");
        goto cleanup9;
    }
    PASS();
cleanup9:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a1); free_module(a2);
}

/* ================================================================== */
/*  Version validation tests                                          */
/* ================================================================== */

static void test_version_validation_pass(void) {
    TEST("version validation succeeds with matching versions");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 2, 0, 0);
    add_dependency(a, "B", false, VERSION_OP_GTE, 1, 5, 0);

    ParserModule *mods[] = { a, b };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 2, g, &err);
    if (rc != DEP_OK) { FAIL("build failed"); goto cleanup10; }

    rc = validate_versions(g, &err);
    if (rc != DEP_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "validate_versions should pass: %s",
                 err.message ? err.message : "(no message)");
        FAIL(buf);
        goto cleanup10;
    }
    PASS();
cleanup10:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b);
}

static void test_version_validation_fail(void) {
    TEST("version validation fails with mismatched versions");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 1, 0, 0);
    add_dependency(a, "B", false, VERSION_OP_GTE, 2, 0, 0);

    ParserModule *mods[] = { a, b };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 2, g, &err);
    if (rc != DEP_OK) { FAIL("build failed"); goto cleanup11; }

    rc = validate_versions(g, &err);
    if (rc != DEP_ERR_VERSION_MISMATCH) {
        FAIL("expected DEP_ERR_VERSION_MISMATCH");
        goto cleanup11;
    }
    if (err.message == NULL) {
        FAIL("expected error message");
        goto cleanup11;
    }
    /* Message should mention both modules and the constraint */
    if (strstr(err.message, "A") == NULL || strstr(err.message, "B") == NULL) {
        FAIL("error message should mention module names");
        goto cleanup11;
    }
    PASS();
cleanup11:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b);
}

static void test_version_caret_validation(void) {
    TEST("version validation with caret constraint");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 1, 5, 2);
    add_dependency(a, "B", false, VERSION_OP_CARET, 1, 2, 0);

    ParserModule *mods[] = { a, b };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 2, g, &err);
    if (rc != DEP_OK) { FAIL("build failed"); goto cleanup12; }

    rc = validate_versions(g, &err);
    if (rc != DEP_OK) {
        FAIL("B 1.5.2 should satisfy ^1.2.0");
        goto cleanup12;
    }
    PASS();
cleanup12:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b);
}

/* ================================================================== */
/*  Composition validation tests                                      */
/* ================================================================== */

static void test_composition_valid(void) {
    TEST("validate_composition succeeds with matched imports/exports");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 1, 0, 0);
    add_dependency(a, "B", false, VERSION_OP_GTE, 1, 0, 0);

    add_export(b, "expr");
    add_export(b, "stmt");
    add_import(a, "expr");

    ParserModule *mods[] = { a, b };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 2, g, &err);
    if (rc != DEP_OK) { FAIL("build failed"); goto cleanup13; }

    uint32_t *order = NULL;
    uint32_t norder = 0;
    rc = resolve_dependencies(g, &order, &norder, &err);
    if (rc != DEP_OK) { FAIL("resolve failed"); goto cleanup13; }

    rc = validate_composition(g, order, norder, &err);
    if (rc != DEP_OK) {
        FAIL("composition should be valid");
        free(order); goto cleanup13;
    }
    free(order);
    PASS();
cleanup13:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b);
}

static void test_composition_missing_symbol(void) {
    TEST("validate_composition fails on missing imported symbol");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 1, 0, 0);
    add_dependency(a, "B", false, VERSION_OP_GTE, 1, 0, 0);

    add_export(b, "expr");
    add_import(a, "nonexistent_symbol");

    ParserModule *mods[] = { a, b };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 2, g, &err);
    if (rc != DEP_OK) { FAIL("build failed"); goto cleanup14; }

    uint32_t *order = NULL;
    uint32_t norder = 0;
    rc = resolve_dependencies(g, &order, &norder, &err);
    if (rc != DEP_OK) { FAIL("resolve failed"); goto cleanup14; }

    rc = validate_composition(g, order, norder, &err);
    if (rc != DEP_ERR_SYMBOL_MISSING) {
        FAIL("expected DEP_ERR_SYMBOL_MISSING");
        free(order); goto cleanup14;
    }
    if (err.message == NULL ||
        strstr(err.message, "nonexistent_symbol") == NULL) {
        FAIL("error should mention the missing symbol name");
        free(order); goto cleanup14;
    }
    free(order);
    PASS();
cleanup14:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b);
}

static void test_composition_transitive_exports(void) {
    TEST("validate_composition with transitive dependency exports");
    /* A imports from C via transitive dep: A -> B -> C */
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 1, 0, 0);
    ParserModule *c = make_module("C", 1, 0, 0);

    add_dependency(a, "B", false, VERSION_OP_GTE, 1, 0, 0);
    add_dependency(b, "C", false, VERSION_OP_GTE, 1, 0, 0);

    add_export(c, "base_expr");
    add_export(b, "extended_expr");
    add_import(a, "base_expr");
    add_import(a, "extended_expr");

    ParserModule *mods[] = { a, b, c };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 3, g, &err);
    if (rc != DEP_OK) { FAIL("build failed"); goto cleanup15; }

    uint32_t *order = NULL;
    uint32_t norder = 0;
    rc = resolve_dependencies(g, &order, &norder, &err);
    if (rc != DEP_OK) { FAIL("resolve failed"); goto cleanup15; }

    rc = validate_composition(g, order, norder, &err);
    if (rc != DEP_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf), "transitive composition should be valid: %s",
                 err.message ? err.message : "(no msg)");
        FAIL(buf);
        free(order); goto cleanup15;
    }
    free(order);
    PASS();
cleanup15:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b); free_module(c);
}

/* ================================================================== */
/*  Module lifecycle tests                                            */
/* ================================================================== */

static void test_module_destroy(void) {
    TEST("parser_module_destroy_contents frees all fields");
    ParserModule *m = make_module("test", 1, 2, 3);
    add_dependency(m, "dep1", false, VERSION_OP_GTE, 1, 0, 0);
    add_export(m, "sym1");
    add_import(m, "sym2");

    parser_module_destroy_contents(m);

    if (m->name != NULL || m->dependencies != NULL ||
        m->exports != NULL || m->imports != NULL) {
        FAIL("fields not zeroed after destroy");
        free(m);
        return;
    }
    free(m);
    PASS();
}

static void test_dep_error_destroy(void) {
    TEST("dep_error_destroy handles populated error");
    DepError err = {0};
    err.code = DEP_ERR_MISSING_DEP;
    err.message = strdup("test error");
    err.module_a = strdup("mod_a");
    err.module_b = strdup("mod_b");

    dep_error_destroy(&err);
    if (err.message != NULL || err.module_a != NULL || err.module_b != NULL) {
        FAIL("fields not zeroed");
        return;
    }
    PASS();
}

static void test_dep_error_destroy_null(void) {
    TEST("dep_error_destroy(NULL) is safe");
    dep_error_destroy(NULL);
    PASS();
}

/* ================================================================== */
/*  No-constraint dependency test                                     */
/* ================================================================== */

static void test_dependency_no_version_constraint(void) {
    TEST("dependency with no version constraint");
    ParserModule *a = make_module("A", 1, 0, 0);
    ParserModule *b = make_module("B", 3, 0, 0);

    /* Add dependency without version constraint (UINT32_MAX signals none) */
    add_dependency(a, "B", false, VERSION_OP_GTE, UINT32_MAX, 0, 0);
    /* Fix: manually clear the constraint since add_dependency sets one */
    free(a->dependencies[0].constraints);
    a->dependencies[0].constraints = NULL;
    a->dependencies[0].nconstraints = 0;

    ParserModule *mods[] = { a, b };
    DependencyGraph *g = dep_graph_create();
    DepError err = {0};

    DepResolveResult rc = build_dependency_graph(mods, 2, g, &err);
    if (rc != DEP_OK) { FAIL("build failed"); goto cleanup16; }

    rc = validate_versions(g, &err);
    if (rc != DEP_OK) {
        FAIL("validation should pass with no constraints");
        goto cleanup16;
    }

    uint32_t *order = NULL;
    uint32_t norder = 0;
    rc = resolve_dependencies(g, &order, &norder, &err);
    if (rc != DEP_OK) { FAIL("resolve failed"); free(order); goto cleanup16; }
    free(order);
    PASS();
cleanup16:
    dep_error_destroy(&err);
    dep_graph_destroy(g);
    free_module(a); free_module(b);
}

/* ================================================================== */
/*  Main                                                              */
/* ================================================================== */

int main(void) {
    printf("Dependency resolver unit tests\n");
    printf("==============================\n\n");

    /* SemVer tests */
    printf("-- SemVer parsing and comparison --\n");
    test_semver_parse_basic();
    test_semver_parse_prerelease();
    test_semver_parse_zero();
    test_semver_parse_invalid();
    test_semver_compare_basic();
    test_semver_compare_prerelease();
    test_semver_satisfies_gte();
    test_semver_satisfies_lt();
    test_semver_satisfies_caret();
    test_semver_satisfies_tilde();
    test_semver_satisfies_caret_zero_major();

    /* Graph construction and resolution */
    printf("\n-- Dependency graph and topological sort --\n");
    test_graph_create_destroy();
    test_graph_empty();
    test_graph_single_module();
    test_graph_linear_chain();
    test_graph_diamond();

    /* Circular dependency detection */
    printf("\n-- Circular dependency detection --\n");
    test_circular_two_nodes();
    test_circular_three_nodes();
    test_no_cycle_in_dag();

    /* Missing dependency handling */
    printf("\n-- Missing dependency handling --\n");
    test_missing_required_dep();
    test_missing_optional_dep();
    test_duplicate_module_name();

    /* Version validation */
    printf("\n-- Version constraint validation --\n");
    test_version_validation_pass();
    test_version_validation_fail();
    test_version_caret_validation();

    /* Composition validation */
    printf("\n-- Composition validation --\n");
    test_composition_valid();
    test_composition_missing_symbol();
    test_composition_transitive_exports();

    /* Lifecycle / safety */
    printf("\n-- Lifecycle and safety --\n");
    test_module_destroy();
    test_dep_error_destroy();
    test_dep_error_destroy_null();
    test_dependency_no_version_constraint();

    printf("\n==============================\n");
    printf("Results: %d/%d passed\n", tests_passed, tests_run);
    printf("==============================\n");

    return (tests_passed == tests_run) ? 0 : 1;
}

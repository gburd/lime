/*
** Test mid-RHS comment preservation in `lime -F`
**
** Note: this test originally relied on assert() for error handling,
** which compiled out under -DNDEBUG (release builds), causing
** silent NULL-deref crashes when lime -F failed for any reason.
** Replaced asserts that GUARD subsequent operations with explicit
** if (cond) { fprintf; abort } so failures are loud regardless of
** NDEBUG.  Pure-correctness asserts (e.g. "comment foo was found")
** stay as asserts since their failure is the intended test signal.
*/
#include "test_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void die(const char *what, const char *detail) {
    fprintf(stderr, "FATAL: %s: %s\n", what, detail ? detail : "(none)");
    fflush(stderr);
    abort();
}

/* Test 1: Format once, verify comments are present */
static void test_format_preserves_comments(const char *lime_exe, const char *grammar_path){
  char *fmt_argv[] = { (char *)lime_exe, "-F", (char *)grammar_path, NULL };
  int rc = 0;
  if (test_compat_run(fmt_argv, &rc) != 0) {
    die("test_compat_run failed to spawn lime", grammar_path);
  }
  if (rc != 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "lime -F exit=%d", rc);
    die("lime -F returned non-zero", buf);
  }

  /* Check that the formatted file has the comments */
  char formatted_path[512];
  snprintf(formatted_path, sizeof(formatted_path), "%s.formatted", grammar_path);

  FILE *f = fopen(formatted_path, "r");
  if (!f) die("fopen failed on .formatted (lime -F did not create it?)",
              formatted_path);
  
  char line[512];
  int found_prelude = 0;
  int found_mid = 0;
  int found_before_minus = 0;
  int found_trailing = 0;
  
  while( fgets(line, sizeof(line), f) ){
    if( strstr(line, "/* prelude */") ) found_prelude = 1;
    if( strstr(line, "/* mid */") ) found_mid = 1;
    if( strstr(line, "/* before MINUS */") ) found_before_minus = 1;
    if( strstr(line, "/* trailing */") ) found_trailing = 1;
  }
  
  fclose(f);
  
  assert(found_prelude && "Missing /* prelude */ comment");
  assert(found_mid && "Missing /* mid */ comment");
  assert(found_before_minus && "Missing /* before MINUS */ comment");
  assert(found_trailing && "Missing /* trailing */ comment");
  
  printf("test_format_preserves_comments: PASS\n");
}

/* Test 2: Format twice, verify idempotence (byte-identical) */
static void test_idempotence(const char *lime_exe, const char *grammar_path){
  char formatted_path[512];
  char twice_path[512];
  
  snprintf(formatted_path, sizeof(formatted_path), "%s.formatted", grammar_path);
  snprintf(twice_path, sizeof(twice_path), "%s.formatted.formatted", grammar_path);
  
  /* Format once */
  {
    char *fmt_argv[] = { (char *)lime_exe, "-F", (char *)grammar_path, NULL };
    int rc = 0;
    if (test_compat_run(fmt_argv, &rc) != 0) die("test_compat_run failed", grammar_path);
    if (rc != 0) die("lime -F first pass returned non-zero", grammar_path);
  }

  /* Format the formatted output */
  {
    char *fmt_argv[] = { (char *)lime_exe, "-F", formatted_path, NULL };
    int rc = 0;
    if (test_compat_run(fmt_argv, &rc) != 0) die("test_compat_run failed", formatted_path);
    if (rc != 0) die("lime -F second pass returned non-zero", formatted_path);
  }

  /* Compare byte-for-byte */
  FILE *f1 = fopen(formatted_path, "rb");
  FILE *f2 = fopen(twice_path, "rb");
  if (!f1) die("fopen failed on .formatted", formatted_path);
  if (!f2) die("fopen failed on .formatted.formatted", twice_path);
  
  int match = 1;
  int c1, c2;
  while( (c1 = fgetc(f1)) != EOF ){
    c2 = fgetc(f2);
    if( c1 != c2 ){
      match = 0;
      break;
    }
  }
  c2 = fgetc(f2);
  if( c2 != EOF ) match = 0;
  
  fclose(f1);
  fclose(f2);
  
  assert(match && "Format not idempotent: format(format(F)) != format(F)");
  
  printf("test_idempotence: PASS\n");
}

int main(int argc, char **argv){
  if( argc < 3 ){
    fprintf(stderr, "Usage: %s <lime-exe> <grammar-path>\n", argv[0]);
    return 1;
  }
  
  const char *lime_exe = argv[1];
  const char *grammar_path = argv[2];
  
  test_format_preserves_comments(lime_exe, grammar_path);
  test_idempotence(lime_exe, grammar_path);
  
  printf("\nAll mid-RHS comment tests passed.\n");
  return 0;
}

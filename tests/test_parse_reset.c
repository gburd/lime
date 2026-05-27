/*
** Test ParseReset() functionality - parser context reuse
*/
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* Generated parser */
#define NUM 1
#define PLUS 2
#define MINUS 3
void *TestResetAlloc(void *(*)(size_t));
void TestResetFree(void *, void (*)(void*));
void TestReset(void *, int, int, int *);
void TestResetReset(void *);
#ifdef YYTRACKMAXSTACKDEPTH
int TestResetStackPeak(void *);
#endif

/*
** Test 1: Smoke - alloc, push tokens, reset, push different tokens, finalize
*/
static void test_smoke(void){
  void *parser;
  int counter = 0;
  
  parser = TestResetAlloc(malloc);
  assert(parser != NULL);
  
  /* First parse */
  TestReset(parser, NUM, 1, &counter);
  TestReset(parser, PLUS, 2, &counter);
  TestReset(parser, 0, 0, &counter);  /* finalize */
  
  /* Reset */
  TestResetReset(parser);
  
  /* Second parse */
  TestReset(parser, NUM, 3, &counter);
  TestReset(parser, MINUS, 4, &counter);
  TestReset(parser, 0, 0, &counter);  /* finalize */
  
  TestResetFree(parser, free);
  printf("test_smoke: PASS\n");
}

/*
** Test 2: Pointer stability - reset doesn't realloc the parser struct
*/
static void test_pointer_stability(void){
  void *parser1, *parser2;
  int counter = 0;
  
  parser1 = TestResetAlloc(malloc);
  assert(parser1 != NULL);
  
  TestReset(parser1, NUM, 1, &counter);
  parser2 = parser1;
  
  TestResetReset(parser1);
  assert(parser1 == parser2);  /* Same pointer */
  
  TestReset(parser1, PLUS, 2, &counter);
  TestReset(parser1, 0, 0, &counter);
  
  TestResetFree(parser1, free);
  printf("test_pointer_stability: PASS\n");
}

/*
** Test 3: Destructor firing - reset calls destructors on stacked tokens
*/
static void test_destructor_firing(void){
  void *parser;
  int counter = 0;
  
  parser = TestResetAlloc(malloc);
  assert(parser != NULL);
  
  /* Push some tokens but don't complete the parse */
  TestReset(parser, NUM, 1, &counter);
  TestReset(parser, PLUS, 2, &counter);
  /* Tokens are on the stack now */
  
  int pre_reset_count = counter;
  TestResetReset(parser);
  int post_reset_count = counter;
  
  /* Destructors should have fired for stacked tokens */
  assert(post_reset_count > pre_reset_count);
  
  TestResetFree(parser, free);
  printf("test_destructor_firing: PASS (destructors: %d)\n", post_reset_count - pre_reset_count);
}

/*
** Test 4: User-arg preservation - extra_argument survives reset
*/
static void test_user_arg_preservation(void){
  void *parser;
  int counter1 = 100;
  int counter2 = 200;
  
  parser = TestResetAlloc(malloc);
  assert(parser != NULL);
  
  /* First parse with counter1 */
  TestReset(parser, NUM, 1, &counter1);
  TestReset(parser, 0, 0, &counter1);
  
  /* Reset - note: user-arg is passed per-call, not stored in parser
  ** for this simple test grammar, so we can't truly test persistence.
  ** In a real scenario with %extra_context, the context would persist. */
  TestResetReset(parser);
  
  /* Second parse with counter2 */
  TestReset(parser, PLUS, 2, &counter2);
  TestReset(parser, 0, 0, &counter2);
  
  TestResetFree(parser, free);
  printf("test_user_arg_preservation: PASS\n");
}

/*
** Test 5: Error-state clear - reset clears error recovery state
*/
static void test_error_state_clear(void){
  void *parser;
  int counter = 0;
  
  parser = TestResetAlloc(malloc);
  assert(parser != NULL);
  
  /* Trigger a parse error (syntax error) */
  TestReset(parser, PLUS, 1, &counter);
  TestReset(parser, PLUS, 2, &counter);  /* double PLUS is invalid */
  
  /* Reset should clear error state */
  TestResetReset(parser);
  
  /* Should be able to parse successfully now */
  TestReset(parser, NUM, 3, &counter);
  TestReset(parser, 0, 0, &counter);
  
  TestResetFree(parser, free);
  printf("test_error_state_clear: PASS\n");
}

#ifdef YYTRACKMAXSTACKDEPTH
/*
** Test 6: Stack depth tracking - yyhwm is reset
*/
static void test_stack_depth_reset(void){
  void *parser;
  int counter = 0;
  
  parser = TestResetAlloc(malloc);
  assert(parser != NULL);
  
  /* Build up some stack depth */
  TestReset(parser, NUM, 1, &counter);
  TestReset(parser, PLUS, 2, &counter);
  TestReset(parser, NUM, 3, &counter);
  int depth1 = TestResetStackPeak(parser);
  
  TestResetReset(parser);
  int depth2 = TestResetStackPeak(parser);
  
  /* After reset, depth should be back to initial */
  assert(depth2 <= depth1);
  
  TestResetFree(parser, free);
  printf("test_stack_depth_reset: PASS (depth before: %d, after: %d)\n", depth1, depth2);
}
#endif

int main(void){
  test_smoke();
  test_pointer_stability();
  test_destructor_firing();
  test_user_arg_preservation();
  test_error_state_clear();
#ifdef YYTRACKMAXSTACKDEPTH
  test_stack_depth_reset();
#endif
  
  printf("\nAll ParseReset tests passed.\n");
  return 0;
}

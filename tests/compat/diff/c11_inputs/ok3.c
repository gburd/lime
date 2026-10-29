enum e { A, B = 2, C, };
_Static_assert(1, "x");
int (*fp)(int, char **);
const char *s = "a" "b";
int g[] = { [0] = 1, [2] = 3 };
void h(void) { int x = _Generic(1, int: 2, default: 3); do { x >>= 1; } while (x); }

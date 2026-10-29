extern int printf(const char *, ...);
static double avg(const double *v, unsigned n) { double s = 0.0; unsigned i; for (i = 0; i < n; ++i) s += v[i]; return n ? s / n : 0.0; }
int main(int argc, char *argv[]) { double v[3] = { 1.0, 2.5e3, .5 }; printf("%f %d\n", avg(v, 3), argc > 1 && argv[1][0] == '-'); return 0x1F & ~argc | 07; }

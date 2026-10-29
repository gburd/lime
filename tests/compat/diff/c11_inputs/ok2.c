struct s { int x; unsigned long y : 3; union { float f; double d; } u; };
static inline int f(struct s *p, ...) { switch (p->x) { case 1: return p->u.f > 0; default: break; } while (p->x--) if (!p) goto out; else continue; out: return sizeof(struct s) + _Alignof(int); }

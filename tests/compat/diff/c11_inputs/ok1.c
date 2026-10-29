int main(void) { int a = 1, b[10]; for (a = 0; a < 10; a++) b[a] = a * 2 + (a ? 1 : 0); return b[3] << 2; }

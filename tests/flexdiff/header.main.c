#define YY_HEADER_EXPORT_START_CONDITIONS
#include <stdio.h>
#include "header.h"
int main(void)
{
	int t, n = 0;
	YY_BUFFER_STATE b = hh_scan_string("ab \"cd\nx\" ef\ngh");
	while ((t = hhlex()))
		n = n * 10 + t;
	printf("%d STR=%d INITIAL=%d line=%d leng=%d\n", n, STR, INITIAL, hhget_lineno(), hhget_leng());
	hh_delete_buffer(b);
	hhlex_destroy();
	return 0;
}

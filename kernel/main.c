#include "print.h"
int main(void) {
	put_char('a');
	put_char('\n');
	put_int(0x01234576);
	put_char('\n');
	put_str("this Is kernel\n");
	while(1);
	return 0;
}

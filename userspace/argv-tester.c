#include <stdio.h>

int main(int argc, char * argv[]) {
	printf("argc = %d\n", argc);
	for (int i = 0; i < argc; ++i) {
		printf("%p argv[%d]= %s\n", argv[i], i, argv[i]);
	}
	printf("continuing until I hit a 0\n");
	int i = argc;
	while (1) {
		printf("argv[%d] = 0x%x\n", i, argv[i]);
		if (argv[i] == 0) {
			break;
		}
		i++;
	}
	return 0;
}

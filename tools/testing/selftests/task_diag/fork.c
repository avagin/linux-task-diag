#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	int i, n;

	if (argc < 2)
		return 1;

	n = atoi(argv[1]);
	for (i = 0; i < n; i++) {
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			printf("Unable to fork: %m\n");
			return 1;
		}
		if (pid == 0) {
			while (1)
				sleep(1000);
			return 0;
		}
	}

	return 0;
}

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

void *f(void *arg)
{
	unsigned long t = (unsigned long) arg;

	sleep(t);
	return NULL;
}

/* usage: fork nproc [mthreads [sleep]] */
int main(int argc, char **argv)
{
	int i, j, n, m = 0;
	unsigned long t_sleep = 1000;
	pthread_attr_t attr;
	pthread_t id;

	if (argc < 2) {
		fprintf(stderr, "usage: fork nproc [mthreads [sleep]]\n");
		return 1;
	}

	n = atoi(argv[1]);

	if (argc > 2)
		m = atoi(argv[2]);

	if (argc > 3)
		t_sleep = atoi(argv[3]);

	pthread_attr_init(&attr);

	for (i = 0; i < n; i++) {
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			printf("Unable to fork: %m\n");
			return 1;
		}
		if (pid == 0) {
			if (m) {
				for (j = 0; j < m-1; ++j)
					pthread_create(&id, &attr, f, (void *)t_sleep);
			}

			sleep(t_sleep);
			return 0;
		}
	}

	return 0;
}

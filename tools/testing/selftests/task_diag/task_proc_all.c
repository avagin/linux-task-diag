#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>


int main(int argc, char **argv)
{
	DIR *d;
	int fd, tasks = 0;
	struct dirent *de;
	char buf[4096];
	int i = 0;

	for (i = 0; i < 10; i++) {
		d = opendir("/proc");
		if (d == NULL)
			return 1;

		while ((de = readdir(d))) {
			if (de->d_name[0] < '0' || de->d_name[0] > '9')
				continue;
			snprintf(buf, sizeof(buf), "/proc/%s/%s", de->d_name, argv[1]);
			fd = open(buf, O_RDONLY);
			read(fd, buf, sizeof(buf));
			close(fd);
			tasks++;
		}

		closedir(d);
	}

	printf("tasks: %d\n", tasks);

	return 0;
}

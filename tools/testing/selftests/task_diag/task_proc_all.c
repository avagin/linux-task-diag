#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>


int main(int argc, char **argv)
{
	DIR *d;
	int fd, tasks = 0;
	struct dirent *de;
	char buf[4096 * 4];
	static const char short_opts[] = "R";
	static struct option long_opts[] = {
		{ "noread",	no_argument,	0, 'R'},
		{}
	};
	bool noread = false;
	int idx, opt;

	while (1) {
		idx = -1;
		opt = getopt_long(argc, argv, short_opts, long_opts, &idx);
		if (opt == -1)
			break;
		switch (opt) {
		case 'R':
			noread = true;
			break;
		default:
			return 1;
		}
	}

	if (optind >= argc)
		return 1;

	d = opendir("/proc");
	if (d == NULL)
		return 1;

	while ((de = readdir(d))) {
		if (de->d_name[0] < '0' || de->d_name[0] > '9')
			continue;
		snprintf(buf, sizeof(buf), "/proc/%s/%s", de->d_name, argv[optind]);
		fd = open(buf, O_RDONLY);
		if (fd < 0)
			return 1;
		if (!noread && read(fd, buf, sizeof(buf)) < 0)
			return 1;
		close(fd);
		tasks++;
	}

	closedir(d);

	printf("tasks: %d\n", tasks);

	return 0;
}

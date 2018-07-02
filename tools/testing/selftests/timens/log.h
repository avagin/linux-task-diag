/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __SELFTEST_TIMENS_LOG_H__
#define __SELFTEST_TIMENS_LOG_H__

#define pr_msg(fmt, lvl, ...)						\
	fprintf(stderr, "[%s] (%s:%d)\t" fmt "\n",			\
			lvl, __FILE__, __LINE__, ##__VA_ARGS__)

#define pr_p(func, fmt, ...)	func(fmt ": %m", ##__VA_ARGS__)

#define pr_err(fmt, ...)						\
	({								\
		pr_msg(fmt, "ERR", ##__VA_ARGS__)			\
		-1;							\
	})
#define pr_fail(fmt, ...)	pr_msg(fmt, "FAIL", ##__VA_ARGS__)

#define pr_perror(fmt, ...)	pr_p(pr_err, fmt, ##__VA_ARGS__)

#endif

// SPDX-License-Identifier: GPL-2.0
/*
 * libisp logging
 *
 * Copyright (C) Google LLC
 */

#ifndef LIBISP_LOG_H_
#define LIBISP_LOG_H_

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/syscall.h>

__attribute__ ((format (printf, 2, 3)))
void __pr_log(int level, const char *fmt, ...);
void pr_log_init(int flags);

extern int log_level;

#define PR_LOG_SYSLOG	1

#define PR_ERROR		0
#define PR_INFO			1
#define PR_DEBUG		2

#define PREFIX			"[%s:%d/%u] "
#define PRERR			PREFIX" ERROR: "
#define PRINF			PREFIX" INFO: "
#define PRDEBUG			PREFIX" DEBUG: "

#define pr_log(l, f, ...)					\
	do {							\
		if ((l) <= log_level)				\
			__pr_log((l), (f), __func__, __LINE__,	\
				syscall(SYS_gettid),		\
				##__VA_ARGS__);			\
	} while (0)

#define pr_debug(f, ...)        \
		pr_log(PR_DEBUG, PRDEBUG f, ##__VA_ARGS__)
#define pr_info(f, ...) \
		pr_log(PR_INFO, PRINF f, ##__VA_ARGS__)
#define pr_err(f, ...)  \
		pr_log(PR_ERROR, PRERR f, ##__VA_ARGS__)

void pr_hex_dump(const void *mem, size_t sz);

#endif /* LIBISP_LOG_H_ */

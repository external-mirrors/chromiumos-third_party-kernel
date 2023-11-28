// SPDX-License-Identifier: GPL-2.0
/*
 * libisp helpers
 *
 * Copyright (C) Google LLC
 */

#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libisp/libisp.h>

typedef void (*log)(int level, const char *fmt, va_list list);

static int syslog_level(int level)
{
	if (level == PR_ERROR)
		return LOG_ERR;
	if (level == PR_INFO)
		return LOG_INFO;
	if (level == PR_DEBUG)
		return LOG_DEBUG;

	return LOG_ERR;
}

static void __pr_log_stdio(int level, const char *fmt, va_list list)
{
	char buf[182];

	vsnprintf(buf, sizeof(buf), fmt, list);
	printf("%s", buf);
}

static void __pr_log_syslog(int level, const char *fmt, va_list list)
{
	vsyslog(syslog_level(level), fmt, list);
}

static log __log = __pr_log_stdio;

void __pr_log(int level, const char *fmt, ...)
{
	va_list list;

	va_start(list, fmt);
	__log(level, fmt, list);
	va_end(list);
}

#define PR_HEX_DUMP_WIDTH       160
void pr_hex_dump(const void *mem, size_t sz)
{
	char xline[PR_HEX_DUMP_WIDTH];
	char sline[PR_HEX_DUMP_WIDTH];
	int xi = 0, si = 0, mi = 0;

	while (mi < sz) {
		char c = *((char *)mem + mi);

		mi++;
		xi += sprintf(xline + xi, "%02X ", 0xff & c);
		if (c > ' ' && c < '~')
			si += sprintf(sline + si, "%c", c);
		else
			si += sprintf(sline + si, ".");
		if (xi >= PR_HEX_DUMP_WIDTH / 2) {
			pr_err("%s         %s\n", xline, sline);
			xi = 0;
			si = 0;
		}
	}

	if (xi) {
		int sz = PR_HEX_DUMP_WIDTH / 2 - xi + 1;

		if (sz > 0) {
			memset(xline + xi, ' ', sz);
			xline[PR_HEX_DUMP_WIDTH / 2 + 1] = 0x00;
		}
		pr_err("%s         %s\n", xline, sline);
	}
}

void pr_log_init(int flag)
{
	static int log_open;

	if (flag == PR_LOG_SYSLOG) {
		if (log_open) {
			closelog();
			log_open = 0;
		}
		openlog("isp", LOG_NDELAY, LOG_LOCAL5);
		__log = __pr_log_syslog;
		log_open = 1;
	}
}

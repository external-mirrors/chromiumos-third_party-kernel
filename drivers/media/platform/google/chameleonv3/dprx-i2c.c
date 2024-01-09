#include <linux/string.h>
#include "dprx.h"

int dprx_i2c_read(struct sink *sink, u8 addr, u8 *buf, int len)
{
	int offset;

	if (addr == 0x50) {
		offset = sink->offset + sink->segment * 256;
		if (len + offset > sink->blocks * 128)
			return -1;
		memcpy(buf, sink->edid + offset, len);
		sink->offset += len;
	} else if (addr == 0x30) {
		if (len == 1)
			buf[0] = sink->segment;
		else if (len > 1)
			return -1;
	}

	return 0;
}

int dprx_i2c_write(struct sink *sink, u8 addr, u8 *buf, int len)
{
	if (addr == 0x50) {
		if (len == 1)
			sink->offset = buf[0];
		else if (len > 1)
			return -1;
	} else if (addr == 0x30) {
		if (len == 1)
			sink->segment = buf[0];
		else if (len > 1)
			return -1;
	} else {
		return -1;
	}

	return 0;
}

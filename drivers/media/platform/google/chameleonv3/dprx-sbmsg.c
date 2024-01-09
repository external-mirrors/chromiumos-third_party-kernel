#include <linux/string.h>
#include "dprx.h"

static u8 get_hdr_crc4(const uint8_t *data, size_t num_nibbles)
{
	u8 bitmask = 0x80;
	u8 bitshift = 7;
	u8 array_index = 0;
	int number_of_bits = num_nibbles * 4;
	u8 remainder = 0;

	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		remainder |= (data[array_index] & bitmask) >> bitshift;
		bitmask >>= 1;
		bitshift--;
		if (bitmask == 0) {
			bitmask = 0x80;
			bitshift = 7;
			array_index++;
		}
		if ((remainder & 0x10) == 0x10)
			remainder ^= 0x13;
	}

	number_of_bits = 4;
	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		if ((remainder & 0x10) != 0)
			remainder ^= 0x13;
	}

	return remainder;
}

static u8 get_body_crc4(const uint8_t *data, u8 number_of_bytes)
{
	u8 bitmask = 0x80;
	u8 bitshift = 7;
	u8 array_index = 0;
	int number_of_bits = number_of_bytes * 8;
	u16 remainder = 0;

	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		remainder |= (data[array_index] & bitmask) >> bitshift;
		bitmask >>= 1;
		bitshift--;
		if (bitmask == 0) {
			bitmask = 0x80;
			bitshift = 7;
			array_index++;
		}
		if ((remainder & 0x100) == 0x100)
			remainder ^= 0xd5;
	}

	number_of_bits = 8;
	while (number_of_bits != 0) {
		number_of_bits--;
		remainder <<= 1;
		if ((remainder & 0x100) != 0)
			remainder ^= 0xd5;
	}

	return remainder & 0xff;
}


void dprx_sbmsg_read(struct dprx_dp *dp, u8 *buf, int len)
{
	int link_count_total;
	int rad_len;
	int hdr_len;
	int body_len;
	bool start;
	bool end;
	int seq_no;
	struct msg_transaction *req;
	struct msg_transaction *rep;

	link_count_total = buf[0] >> 4;
	rad_len = link_count_total / 2;
	hdr_len = rad_len + 3;
	body_len = buf[rad_len + 1] & 0x3f;

	/* If message is incomplete, do nothing */
	if (hdr_len + body_len > len)
		return;

	start  = (buf[rad_len + 2] >> 7) & 1;
	end    = (buf[rad_len + 2] >> 6) & 1;
	seq_no = (buf[rad_len + 2] >> 4) & 1;

	req = &dp->mt_req[seq_no];
	rep = &dp->mt_rep[seq_no];

	if (start)
		req->len = 0;
	/* TODO: check overflow */
	memcpy(req->buf + req->len, buf + hdr_len, body_len - 1);
	req->len += body_len - 1;

	if (end) {
		rep->written = 0;
		memcpy(rep->rad, buf + 1, rad_len);
		rep->link_count_total = link_count_total;
		dprx_mt_execute(dp, req, rep);
	}
}

void dprx_sbmsg_write(struct dprx_dp *dp, u8 *buf, int buf_len)
{
	int rad_len;
	int hdr_len;
	int body_len;
	bool start;
	bool end;
	u8 hdr_crc4;
	u8 body_crc4;
	struct msg_transaction *rep;

	rep = &dp->mt_rep[dp->mt_seq_no];
	if (rep->len == 0) {
		dp->mt_seq_no ^= 1;
		rep = &dp->mt_rep[dp->mt_seq_no];
		if (rep->len == 0)
			return;
	}

	rad_len = rep->link_count_total / 2;
	hdr_len = rad_len + 3;
	body_len = min(rep->len - rep->written + 1, buf_len - hdr_len);

	start = (rep->written == 0);
	end   = (rep->written + body_len - 1 == rep->len);

	buf[0] = rep->link_count_total << 4 | ((rep->link_count_total - 1) & 0xf);
	memcpy(buf + 1, rep->rad, rad_len);
	buf[rad_len + 1] = body_len;
	buf[rad_len + 2] = start << 7 | end << 6 | dp->mt_seq_no << 4;
	hdr_crc4 = get_hdr_crc4(buf, hdr_len * 2 - 1);
	buf[rad_len + 2] |= hdr_crc4;
	memcpy(buf + hdr_len, rep->buf + rep->written, body_len - 1);
	body_crc4 = get_body_crc4(buf + hdr_len, body_len - 1);
	buf[hdr_len + body_len - 1] = body_crc4;
	rep->written += body_len - 1;

	if (end) {
		rep->len = 0;
		rep->written = 0;
		dp->mt_seq_no ^= 1;
	}
}

bool dprx_sbmsg_pending(struct dprx_dp *dp)
{
	return dp->mt_rep[0].len > 0 || dp->mt_rep[1].len > 0;
}

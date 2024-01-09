#include <linux/kernel.h>

struct dprx_dp_cfg {
	const char *reg_core;
	const char *reg_irq;
	const char *irq;
	int has_mst;
	int sink_count;
};

struct msg_transaction {
       u8 buf[256];
       int len;
       int written;
       u8 rad[16];
       int link_count_total;
};

struct dpcd_mem {
       u8 caps[0x10];        /* 00000 - 0000f */
       u8 mstm_cap;          /* 00021         */
       u8 guid[0x10];        /* 00030 - 0003f */
       u8 link_conf[0x3];    /* 00100 - 00102 */
       u8 mstm_ctrl;         /* 00111         */
       u8 vc_alloc[0x3];     /* 001c0 - 001c2 */
       u8 sink_count;        /* 00200         */
       u8 irq_vector;        /* 00201         */
       u8 lane_align_status; /* 00204         */
       u8 vc_table_status;   /* 0x2c0         */
       u8 vc_table[0x40];    /* 002c1 - 002ff */
       u8 sink_spec[0xc];    /* 00400 - 0040b */
       u8 down_req[0x30];    /* 01000 - 01030 */
       u8 down_rep[0x30];    /* 01400 - 01430 */
};

#define DPRX_MAX_EDID_BLOCKS 4

struct sink {
       u8 edid[128 * DPRX_MAX_EDID_BLOCKS];
       int blocks;
       int offset;
       int segment;
};

struct dprx_dp {
       struct device *dev;
       void __iomem *iobase;
       void __iomem *iobase_irq;

       struct sink sinks[4];
       u8 vc_id[4];
       int sink_count;
       int has_mst;
       int total_pbn;
       int sum_pbn;

       /* dpcd */
       struct dpcd_mem dpcd;

       /* msg transaction */
       struct msg_transaction mt_req[2];
       struct msg_transaction mt_rep[2];
       bool mt_seq_no;
};

int dprx_dp_init(struct dprx_dp *dp, struct device *dev,
		 const struct dprx_dp_cfg *cfg);

#define AUX_ACK 0x0
#define AUX_I2C_NACK 0x4

struct aux_msg {
	u8 cmd;
	u32 addr;
	u8 len;
	u8 data[16];
};

/* dprx-aux.c */
void dprx_aux_handle_request(struct dprx_dp *dp, struct aux_msg *req,
			     struct aux_msg *res);
int dprx_aux_read_request(struct dprx_dp *dp, struct aux_msg *req);
void dprx_aux_write_response(struct dprx_dp *dp, struct aux_msg *res);

/* dprx-dpcd.c */
void dprx_dpcd_init(struct dprx_dp *dp);
void dprx_dpcd_access(struct dprx_dp *dp, struct aux_msg *req,
		      struct aux_msg *res);


/* dprx-dprx.c */
void dprx_dprx_set_hpd(struct dprx_dp *dp, int val);
int dprx_dprx_get_hpd(struct dprx_dp *dp);
void dprx_dprx_pulse_hpd(struct dprx_dp *dp);
void dprx_dprx_set_link_rate(struct dprx_dp *dp, int val);
void dprx_dprx_set_lane_count(struct dprx_dp *dp, int val);
void dprx_dprx_set_training_pattern(struct dprx_dp *dp, int val);
void dprx_dprx_set_scrambler(struct dprx_dp *dp, int val);
int dprx_dprx_get_cr_lock(struct dprx_dp *dp);
int dprx_dprx_get_sym_lock(struct dprx_dp *dp);
int dprx_dprx_get_interlane_align(struct dprx_dp *dp);
int dprx_dprx_get_sink_status(struct dprx_dp *dp);
int dprx_dprx_get_rx_busy(struct dprx_dp *dp);
void dprx_dprx_set_mst(struct dprx_dp *dp, int val);
void dprx_dprx_clear_vc_payload_table(struct dprx_dp *dp);
void dprx_dprx_set_vc_payload_table(struct dprx_dp *dp, u8 *table, u8 *id);
int dprx_dprx_get_act(struct dprx_dp *dp);
void dprx_dprx_clear_act(struct dprx_dp *dp);
int dprx_dprx_read_aux(struct dprx_dp *dp, u8 *data);
void dprx_dprx_write_aux(struct dprx_dp *dp, u8 *data, int length);
void dprx_dprx_init(struct dprx_dp *dp);

/* dprx-edid.c */
extern u8 default_edid[256];
extern u8 default_edid_blocks;

/* dprx-i2c.c */
int dprx_i2c_read(struct sink *sink, u8 addr, u8 *buf, int len);
int dprx_i2c_write(struct sink *sink, u8 addr, u8 *buf, int len);

/* dprx-mt.c */
void dprx_mt_execute(struct dprx_dp *dp, struct msg_transaction *req,
		     struct msg_transaction *rep);

/* dprx-sbmsg.c */
void dprx_sbmsg_read(struct dprx_dp *dp, u8 *buf, int len);
void dprx_sbmsg_write(struct dprx_dp *dp, u8 *buf, int buf_len);
bool dprx_sbmsg_pending(struct dprx_dp *dp);

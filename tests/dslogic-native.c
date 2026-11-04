/* Hardware-free regression tests exercising the real driver translation units.
 * USB/session/registry boundaries are mocked; GLib allocation/lists are real. */
#include <stdarg.h>
#include "../src/hardware/dreamsourcelab-dslogic/protocol.c"
#undef SR_REGISTER_DEV_DRIVER
#define SR_REGISTER_DEV_DRIVER(x)
#include "../src/hardware/dreamsourcelab-dslogic/api.c"

static struct sr_trigger *test_trigger;
static struct sr_dev_inst *active;
static struct libusb_transfer *queued[128];
static int submits, fail_submit, fail_write, writes, bulk_count, short_bulk;
static int headers, ends, triggers, source_count, reentrant_start;
static uint8_t bulk[2][512], result[8192];
static size_t bulk_len[2], result_samples;
static struct dsl_logic_decoder result_decoder;

int sr_log(int level, const char *format, ...) { (void)level; (void)format; return SR_OK; }
const char * LIBUSB_CALL libusb_error_name(int error) { (void)error; return "mock"; }
int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle *h, unsigned char ep,
	unsigned char *data, int length, int *transferred, unsigned int timeout)
{
	(void)h; (void)timeout; assert(ep == 2); assert(bulk_count < 2);
	assert(length <= 512); memcpy(bulk[bulk_count], data, length);
	bulk_len[bulk_count++] = length; *transferred = length - short_bulk; return 0;
}
struct libusb_transfer * LIBUSB_CALL libusb_alloc_transfer(int n)
{ (void)n; return g_malloc0(sizeof(struct libusb_transfer)); }
void LIBUSB_CALL libusb_free_transfer(struct libusb_transfer *t) { g_free(t); }
int LIBUSB_CALL libusb_submit_transfer(struct libusb_transfer *t)
{
	int i; if (++submits == fail_submit) return LIBUSB_ERROR_IO;
	for (i = 0; i < 128; i++) if (!queued[i]) { queued[i] = t; return 0; }
	assert(0); return -1;
}
int LIBUSB_CALL libusb_cancel_transfer(struct libusb_transfer *t)
{ t->status = LIBUSB_TRANSFER_CANCELLED; return 0; }
int LIBUSB_CALL libusb_handle_events_timeout(libusb_context *c, struct timeval *tv)
{
	int i; (void)c; (void)tv;
	for (i = 0; i < 128; i++) if (queued[i] && queued[i]->status == LIBUSB_TRANSFER_CANCELLED) {
		struct libusb_transfer *t = queued[i]; queued[i] = NULL; t->callback(t);
	}
	return 0;
}
int command_ctl_wr(libusb_device_handle *h, struct ctl_wr_cmd cmd)
{
	(void)h;
	if (++writes == fail_write) return SR_ERR;
	if (cmd.header.dest == DSL_CTL_START && reentrant_start) {
		struct libusb_transfer *t = queued[0];
		struct dsl_trigger_pos *p = (void *)t->buffer;
		queued[0] = NULL; memset(p, 0, t->length);
		WL32(&p->check_id, TRIG_CHECKID); WL32(&p->status, 1);
		t->actual_length = t->length; t->status = LIBUSB_TRANSFER_COMPLETED;
		t->callback(t);
		t = queued[1]; queued[1] = NULL;
		memset(t->buffer, 0xff, t->length); t->actual_length = t->length;
		t->status = LIBUSB_TRANSFER_COMPLETED; t->callback(t);
	}
	return SR_OK;
}
int command_ctl_rd(libusb_device_handle *h, struct ctl_rd_cmd cmd)
{ (void)h; memset(cmd.data, 0, cmd.header.size); cmd.data[0] = bmSYS_CLR | bmGPIF_DONE; return SR_OK; }
int LIBUSB_CALL libusb_release_interface(libusb_device_handle *h, int i) { (void)h; (void)i; return 0; }
void LIBUSB_CALL libusb_close(libusb_device_handle *h) { (void)h; }
int sr_resource_open(struct sr_context *c, struct sr_resource *r, int type, const char *name)
{ (void)c; (void)r; (void)type; (void)name; return SR_ERR; }
int sr_resource_close(struct sr_context *c, struct sr_resource *r) { (void)c; (void)r; return SR_OK; }
gssize sr_resource_read(struct sr_context *c, const struct sr_resource *r, void *b, size_t n)
{ (void)c; (void)r; (void)b; (void)n; return -1; }
GSList *sr_resourcepaths_get(int type) { (void)type; return NULL; }
struct sr_trigger *sr_session_trigger_get(struct sr_session *s) { (void)s; return test_trigger; }
int sr_session_trigger_set(struct sr_session *s, struct sr_trigger *t) { (void)s; test_trigger = t; return SR_OK; }
int usb_source_add(struct sr_session *s, struct sr_context *c, int timeout,
	sr_receive_data_callback cb, void *data)
{ (void)s; (void)c; (void)timeout; (void)cb; (void)data; source_count++; return SR_OK; }
int usb_source_remove(struct sr_session *s, struct sr_context *c)
{ (void)s; (void)c; assert(source_count > 0); source_count--; return SR_OK; }
int std_session_send_df_header(const struct sr_dev_inst *s) { (void)s; headers++; return SR_OK; }
int std_session_send_df_end(const struct sr_dev_inst *s) { (void)s; assert(headers > ends); ends++; return SR_OK; }
int std_session_send_df_trigger(const struct sr_dev_inst *s) { (void)s; assert(headers > ends); triggers++; return SR_OK; }
int sr_session_send(const struct sr_dev_inst *s, const struct sr_datafeed_packet *p)
{
	struct dev_context *d = s->priv;
	if (p->type == SR_DF_LOGIC) {
		const struct sr_datafeed_logic *l = p->payload;
		assert(headers > ends);
		if (l->format == LA_CROSS_DATA) {
			assert(l->block_bytes == 8);
			result_samples += dsl_logic_decode(&result_decoder, l->data, l->length,
				d->capture_channel_mask, d->sample_unitsize, result + result_samples * d->sample_unitsize);
		} else {
			assert(l->unitsize == d->sample_unitsize);
			memcpy(result + result_samples * l->unitsize, l->data, l->length);
			result_samples += l->length / l->unitsize;
		}
	}
	return SR_OK;
}
struct sr_channel *sr_channel_new(struct sr_dev_inst *s, int index, int type, gboolean enabled, const char *name)
{
	struct sr_channel *c = g_malloc0(sizeof(*c)); c->sdi = s; c->index = index;
	c->type = type; c->enabled = enabled; c->name = g_strdup(name);
	s->channels = g_slist_append(s->channels, c); return c;
}
void sr_channel_free(struct sr_channel *c) { g_free(c->name); g_free(c->priv); g_free(c); }
struct sr_channel_group *sr_channel_group_new(struct sr_dev_inst *s, const char *name, void *priv)
{
	struct sr_channel_group *g = g_malloc0(sizeof(*g)); g->name = g_strdup(name); g->priv = priv;
	s->channel_groups = g_slist_append(s->channel_groups, g); return g;
}
int std_str_idx(GVariant *v, const char **array, unsigned int n)
{ unsigned int i; for (i = 0; i < n; i++) if (!strcmp(g_variant_get_string(v, NULL), array[i])) return i; return -1; }
int std_u64_idx(GVariant *v, const uint64_t *array, unsigned int n)
{ unsigned int i; for (i = 0; i < n; i++) if (g_variant_get_uint64(v) == array[i]) return i; return -1; }
int std_double_tuple_idx(GVariant *v, const double array[][2], unsigned int n)
{ (void)v; (void)array; (void)n; return -1; }
int sr_analog_init(struct sr_datafeed_analog *a, struct sr_analog_encoding *e,
	struct sr_analog_meaning *m, struct sr_analog_spec *s, int digits)
{ (void)digits; memset(a, 0, sizeof(*a)); a->encoding=e; a->meaning=m; a->spec=s; return SR_OK; }

/* Discovery/open are deliberately unavailable: this binary cannot touch USB. */
ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context *c, libusb_device ***l) { (void)c; (void)l; return -1; }
int LIBUSB_CALL libusb_get_device_descriptor(libusb_device *d, struct libusb_device_descriptor *v) { (void)d; (void)v; return -1; }
int LIBUSB_CALL libusb_open(libusb_device *d, libusb_device_handle **h) { (void)d; (void)h; return -1; }
void LIBUSB_CALL libusb_free_device_list(libusb_device **l,int u) { (void)l; (void)u; }
int LIBUSB_CALL libusb_has_capability(uint32_t c) { (void)c; return 0; }
int LIBUSB_CALL libusb_kernel_driver_active(libusb_device_handle *h,int i) { (void)h; (void)i; return 0; }
int LIBUSB_CALL libusb_detach_kernel_driver(libusb_device_handle *h,int i) { (void)h; (void)i; return -1; }
uint8_t LIBUSB_CALL libusb_get_device_address(libusb_device *d) { (void)d; return 0; }
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *h,int i) { (void)h; (void)i; return -1; }
int LIBUSB_CALL libusb_get_string_descriptor_ascii(libusb_device_handle *h,uint8_t i,unsigned char *b,int n)
{ (void)h; (void)i; (void)b; (void)n; return -1; }
int LIBUSB_CALL libusb_control_transfer(libusb_device_handle *h,uint8_t a,uint8_t b,uint16_t c,uint16_t d,unsigned char *e,uint16_t f,unsigned int g)
{ (void)h;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; return -1; }
int usb_get_port_path(libusb_device *d, char *p, int n) { (void)d; (void)p; (void)n; return -1; }
void sr_usb_dev_inst_free(struct sr_usb_dev_inst *u) { g_free(u); }
void sr_dev_inst_free(struct sr_dev_inst *s) { (void)s; assert(0); }

static void reset_mock(void)
{
	memset(queued, 0, sizeof(queued)); submits=fail_submit=writes=fail_write=bulk_count=short_bulk=0;
	headers=ends=triggers=source_count=reentrant_start=0; result_samples=0;
	memset(&result_decoder, 0, sizeof(result_decoder)); test_trigger=NULL;
}
static void init_device(struct sr_dev_inst *s, struct dev_context *d, struct sr_usb_dev_inst *u)
{
	unsigned int i;
	memset(s,0,sizeof(*s)); memset(d,0,sizeof(*d)); memset(u,0,sizeof(*u));
	s->priv=d; s->conn=u; s->session=(void *)1; u->devhdl=(void *)1;
	for (i=0; supported_device[i].vid; i++) if (!strcmp(supported_device[i].model,"DSLogic U3Pro32") &&
		supported_device[i].usb_speed == LIBUSB_SPEED_SUPER) break;
	assert(supported_device[i].vid); d->profile=&supported_device[i];
	d->mode=DSL_MODE_LOGIC; d->ch_mode=DSL_BUFFER250x32; d->cur_samplerate=SR_MHZ(250);
	d->limit_samples=d->actual_samples=128; d->sample_unitsize=4; d->rle_support=TRUE;
	assert(dsl_setup_probes(s,32)==SR_OK); d->capture_channel_mask=UINT32_MAX; d->capture_channel_count=32;
	d->deinterleave_buffer=g_malloc0(256); active=s;
}
static void dispose_device(struct sr_dev_inst *s)
{
	GSList *l; struct dev_context *d=s->priv;
	g_free(d->deinterleave_buffer); d->deinterleave_buffer=NULL;
	for (l = s->channels; l; l = l->next)
		sr_channel_free(l->data);
	g_slist_free(s->channels);
	for(l=s->channel_groups;l;l=l->next) { struct sr_channel_group *g=l->data; g_slist_free(g->channels); g_free(g->name); g_free(g); }
	g_slist_free(s->channel_groups);
}
static void test_probes_and_cross(void)
{
	struct sr_dev_inst s; struct dev_context d; struct sr_usb_dev_inst u;
	uint8_t input[32*8*2]; size_t split; unsigned int i;
	init_device(&s,&d,&u);
	assert(dsl_adjust_probes(&s,8)==SR_OK); assert(g_slist_length(s.channels)==8);
	assert(dsl_adjust_probes(&s,32)==SR_OK); assert(g_slist_length(s.channels)==32);
	assert(g_slist_length(((struct sr_channel_group *)s.channel_groups->data)->channels)==32);
	for(i=0;i<32;i++) assert(((struct sr_channel *)g_slist_nth_data(s.channels,i))->index==(int)i);
	memset(input,0,sizeof(input)); memset(input+31*8,0xff,8); memset(input+32*8+31*8,0xff,8);
	for(split=1;split<sizeof(input);split++) {
		d.sent_samples=0; d.decoder.used=0; d.limit_samples=65; d.actual_samples=128;
		result_samples=0; headers=1; ends=0;
		receive_logic_bytes(&s,input,split); receive_logic_bytes(&s,input+split,sizeof(input)-split);
		assert(result_samples==65 && d.sent_samples==65 && !d.decoder.used);
		for(i=0;i<65;i++) assert(RL32(result+i*4)==UINT32_C(0x80000000));
	}
	/* Sparse physical channel 31, one byte per USB completion. */
	for(i=0;i<31;i++) ((struct sr_channel *)g_slist_nth_data(s.channels,i))->enabled=FALSE;
	d.capture_channel_mask=UINT32_C(0x80000000); d.capture_channel_count=1; d.sent_samples=0; d.decoder.used=0; result_samples=0;
	memset(input,0xff,16); for(i=0;i<16;i++) receive_logic_bytes(&s,input+i,1);
	assert(result_samples==65); for(i=0;i<65;i++) assert(RL32(result+i*4)==UINT32_C(0x80000000));
	dispose_device(&s);
}
static void test_arm(void)
{
	struct sr_dev_inst s; struct dev_context d; struct sr_usb_dev_inst u;
	struct sr_trigger t={0}; struct sr_trigger_stage st[2]={{0}}; struct sr_trigger_match m[2]={{0}};
	struct DSL_setting *base; struct DSL_setting_ext32 *ext;
	init_device(&s,&d,&u);
	m[0].channel=g_slist_nth_data(s.channels,31); m[0].match=SR_TRIGGER_EDGE;
	m[1].channel=g_slist_nth_data(s.channels,0); m[1].match=SR_TRIGGER_RISING;
	st[0].matches=g_slist_append(NULL,&m[0]); st[1].matches=g_slist_append(NULL,&m[1]);
	t.stages=g_slist_append(NULL,&st[0]); t.stages=g_slist_append(t.stages,&st[1]); test_trigger=&t;
	assert(dsl_fpga_arm(&s)==SR_OK); assert(bulk_count==2);
	base=(void *)bulk[0]; ext=(void *)bulk[1];
	assert(bulk_len[0]==sizeof(*base) && bulk_len[1]==sizeof(*ext));
	assert(ext->trig_header==0x6060 && ext->align_bytes==0xffff);
	assert(ext->trig_mask0[0]==0xffff && ext->trig_edge0[0]==0x8000);
	assert(base->trig_mask0[1]==0xfffe && base->trig_value0[1]==1 && base->trig_edge0[1]==1);
	assert(base->trig_glb==2);
	bulk_count=0; short_bulk=1; assert(dsl_fpga_arm(&s)!=SR_OK); short_bulk=0;
	g_slist_free(st[0].matches); g_slist_free(st[1].matches); g_slist_free(t.stages); test_trigger=NULL;
	dispose_device(&s);
}
static void test_start_lifetime(void)
{
	struct sr_dev_inst s; struct dev_context d; struct sr_usb_dev_inst u;
	struct sr_context ctx={0}; struct drv_context dc={0}; struct sr_dev_driver di={0}; int i;
	init_device(&s,&d,&u); g_free(d.deinterleave_buffer); d.deinterleave_buffer=NULL;
	dc.sr_ctx=&ctx; di.context=&dc; s.driver=&di;
	for(i=1;i<=3;i++) {
		reset_mock(); fail_submit=i;
		assert(dslogic_acquisition_start(&s)!=SR_OK);
		assert(!d.submitted_transfers && !d.source_added && !d.transfers && !d.deinterleave_buffer);
	}
	for(i=0;i<3;i++) {
		reset_mock(); reentrant_start=1;
		assert(dslogic_acquisition_start(&s)==SR_OK);
		assert(dsl_drain_transfers(&s)==SR_OK);
		assert(headers==1 && ends==1 && triggers==1);
		assert(d.status==DSL_FINISH && !d.transfers && !d.submitted_transfers && !source_count);
	}
	dispose_device(&s);
}
int main(void)
{
	uint8_t code;
	assert(sizeof(struct DSL_setting)==372); assert(sizeof(struct DSL_setting_ext32)==204);
	assert(dsl_threshold_code(1.0,0,&code)&&code==46);
	assert(dsl_threshold_code(1.0,1,&code)&&code==38);
	assert(!dsl_threshold_code(NAN,0,&code)); assert(!dsl_threshold_code(5.1,0,&code));
	assert(dsl_trigger_replicate(0xfff2,1,0)==0x2222);
	assert(dsl_trigger_replicate(0xff81,0,1)==0x8181);
	reset_mock(); test_probes_and_cross(); reset_mock(); test_arm(); reset_mock(); test_start_lifetime();
	puts("DSLogic native: cross fragmentation/32-bit sparse tails, probes, arm/ext32, repeated acquisition and failed-start drain PASS");
	return 0;
}

/* PXView advanced-trigger JSON -> DSView FPGA predicates. GPL-3.0-or-later.
 * No new JSON dependency: bounded parser for the documented TriggerConfig
 * schema, rejecting malformed, duplicate, out-of-range and unknown fields. */
#ifndef DSLOGIC_TRIGGER_H
#define DSLOGIC_TRIGGER_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

struct dsl_trigger_stage {
	uint32_t mask[2], value[2], edge[2], count;
	uint16_t logic[2];
};
struct dsl_adv_trigger {
	unsigned int mode, count, position;
	struct dsl_trigger_stage stage[16];
};
struct dsl_json_token { const char *text; size_t len; int next; char type; };
struct dsl_json { const char *p; struct dsl_json_token t[512]; int n; };
static inline void dsl_json_space(struct dsl_json *j)
{ while (*j->p && isspace((unsigned char)*j->p)) j->p++; }
static int dsl_json_value(struct dsl_json *j, unsigned int depth)
{
	int idx, child;
	char close, type;
	const char *start;
	if (depth > 6 || j->n == 512) return -1;
	dsl_json_space(j);
	idx = j->n++;
	type = *j->p;
	j->t[idx].type = type;
	if (type == '{' || type == '[') {
		close = type == '{' ? '}' : ']';
		j->p++; dsl_json_space(j);
		if (*j->p != close) for (;;) {
			if (type == '{') {
				child = dsl_json_value(j, depth + 1);
				if (child < 0 || j->t[child].type != '"') return -1;
				dsl_json_space(j);
				if (*j->p++ != ':') return -1;
			}
			if (dsl_json_value(j, depth + 1) < 0) return -1;
			dsl_json_space(j);
			if (*j->p == close) break;
			if (*j->p++ != ',') return -1;
		}
		j->p++;
	} else if (type == '"') {
		start = ++j->p;
		while (*j->p && *j->p != '"') {
			/* TriggerConfig has no escaped keys or predicate characters. */
			if ((unsigned char)*j->p < 32 || *j->p == '\\') return -1;
			j->p++;
		}
		if (!*j->p) return -1;
		j->t[idx].text = start; j->t[idx].len = j->p++ - start;
	} else {
		start = j->p;
		while (*j->p && !strchr(",]} \t\r\n", *j->p)) j->p++;
		j->t[idx].text = start; j->t[idx].len = j->p - start;
		if (!j->t[idx].len) return -1;
	}
	j->t[idx].next = j->n;
	return idx;
}
static inline int dsl_json_eq(const struct dsl_json_token *t, const char *s)
{ return t->len == strlen(s) && !memcmp(t->text, s, t->len); }
static inline int dsl_json_uint(const struct dsl_json_token *t, uint32_t max, uint32_t *out)
{
	size_t i; uint64_t n = 0;
	if (t->type == '"' || !t->len || (t->len > 1 && t->text[0] == '0')) return 0;
	for (i = 0; i < t->len; i++) {
		if (t->text[i] < '0' || t->text[i] > '9') return 0;
		n = n * 10 + t->text[i] - '0';
		if (n > max) return 0;
	}
	*out = (uint32_t)n; return 1;
}
/* Predicates are displayed MSB first, optionally separated by spaces. */
static inline int dsl_trigger_pattern(const struct dsl_json_token *t,
	uint32_t *mask, uint32_t *value, uint32_t *edge, unsigned int *width)
{
	size_t i;
	*mask = UINT32_MAX; *value = *edge = 0; *width = 0;
	if (t->type != '"') return 0;
	for (i = 0; i < t->len; i++) {
		char c = t->text[i];
		if (c == ' ') continue;
		if (!strchr("X01RFC", c) || ++*width > 32) return 0;
		*mask = (*mask << 1) | (c == 'X' || c == 'C');
		*value = (*value << 1) | (c == '1' || c == 'R');
		*edge = (*edge << 1) | (c == 'R' || c == 'F' || c == 'C');
	}
	return *width != 0;
}
static inline int dsl_trigger_parse(const char *text, unsigned int channels,
	struct dsl_adv_trigger *out)
{
	struct dsl_json j = {0};
	struct dsl_adv_trigger cfg = {0};
	int i, a = -1, stage_index = 0;
	unsigned int seen = 0;
	uint32_t n, count1;
	if (!text || strlen(text) > 16384 || !channels || channels > 32) return 0;
	j.p = text;
	if (dsl_json_value(&j, 0) != 0 || j.t[0].type != '{') return 0;
	dsl_json_space(&j); if (*j.p) return 0;
	for (i = 1; i < j.t[0].next; ) {
		struct dsl_json_token *key = &j.t[i], *v = &j.t[i + 1];
		unsigned int field;
		if (dsl_json_eq(key, "mode")) { field = 1; if (!dsl_json_uint(v, 2, &n)) return 0; cfg.mode = n; }
		else if (dsl_json_eq(key, "stage_count")) { field = 2; if (!dsl_json_uint(v, 16, &n) || !n) return 0; cfg.count = n; }
		else if (dsl_json_eq(key, "trigger_pos")) { field = 4; if (!dsl_json_uint(v, 100, &n)) return 0; cfg.position = n; }
		else if (dsl_json_eq(key, "stages")) { field = 8; if (v->type != '[') return 0; a = i + 1; }
		else if (dsl_json_eq(key, "adv_enabled")) { field = 16; if (!dsl_json_eq(v, "true") && !dsl_json_eq(v, "false")) return 0; }
		else if (dsl_json_eq(key, "adv_tab_index")) { field = 32; if (!dsl_json_uint(v, 1, &n)) return 0; }
		else if (dsl_json_eq(key, "serial_data_channel")) { field = 64; if (!dsl_json_uint(v, 31, &n)) return 0; }
		else if (dsl_json_eq(key, "serial_bits")) { field = 128; if (!dsl_json_uint(v, 16, &n)) return 0; }
		else if (dsl_json_eq(key, "serial_value")) { field = 256; if (v->type != '"') return 0; }
		else return 0;
		if (seen & field) return 0;
		seen |= field;
		i = v->next;
	}
	if ((seen & 15) != 15 || cfg.mode == 0 || (cfg.mode == 2 && cfg.count != 4)) return 0;
	for (i = a + 1; i < j.t[a].next; i = j.t[i].next) {
		struct dsl_trigger_stage *st;
		int k; unsigned int fields = 0, widths[2] = {0};
		if (stage_index >= 16 || j.t[i].type != '{') return 0;
		st = &cfg.stage[stage_index];
		st->mask[0] = st->mask[1] = UINT32_MAX;
		count1 = 0;
		for (k = i + 1; k < j.t[i].next; ) {
			struct dsl_json_token *key = &j.t[k], *v = &j.t[k + 1];
			unsigned int field;
			if (dsl_json_eq(key, "value0")) { field = 1; if (!dsl_trigger_pattern(v, &st->mask[0], &st->value[0], &st->edge[0], &widths[0])) return 0; }
			else if (dsl_json_eq(key, "value1")) { field = 2; if (!dsl_trigger_pattern(v, &st->mask[1], &st->value[1], &st->edge[1], &widths[1])) return 0; }
			else if (dsl_json_eq(key, "logic")) { field = 4; if (!dsl_json_uint(v, 3, &n)) return 0; st->logic[0] |= n << 1; st->logic[1] |= n << 1; }
			else if (dsl_json_eq(key, "inv0")) { field = 8; if (!dsl_json_uint(v, 1, &n)) return 0; st->logic[0] |= n; }
			else if (dsl_json_eq(key, "inv1")) { field = 16; if (!dsl_json_uint(v, 1, &n)) return 0; st->logic[1] |= n; }
			else if (dsl_json_eq(key, "count0")) { field = 32; if (!dsl_json_uint(v, UINT32_MAX, &st->count)) return 0; }
			else if (dsl_json_eq(key, "count1")) { field = 64; if (!dsl_json_uint(v, UINT32_MAX, &count1)) return 0; }
			else return 0;
			if (fields & field) return 0;
			fields |= field;
			k = v->next;
		}
		/* DSView's DSL_setting has only count0: never silently drop count1. */
		if (fields != 127 || count1 || widths[0] != widths[1]) return 0;
		if (cfg.mode == 2 && stage_index >= 2) {
			if (widths[0] != 16) return 0;
		} else if (widths[0] != channels) return 0;
		stage_index++;
	}
	if (stage_index < (int)cfg.count) return 0;
	*out = cfg; return 1;
}
#endif

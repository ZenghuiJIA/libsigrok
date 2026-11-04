/* DSLogic logic wire helpers. GPL-3.0-or-later.
 * Cross data is little endian: 64 samples per enabled channel, in channel
 * order. RLE is expanded by the FPGA before this USB representation. */
#ifndef DSLOGIC_LOGIC_H
#define DSLOGIC_LOGIC_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

struct dsl_logic_decoder {
	uint8_t pending[32 * 8];
	size_t used;
};

/* Output uses physical channel indexes, never a compacted enabled index. */
static inline size_t dsl_logic_decode(struct dsl_logic_decoder *state,
	const uint8_t *src, size_t length, uint32_t mask, unsigned int unitsize,
	uint8_t *dst)
{
	size_t count = 0, atom = 0;
	unsigned int ch, bit, byte;
	for (ch = 0; ch < 32; ch++)
		if (mask & (UINT32_C(1) << ch)) atom += 8;
	if (!atom || (unitsize != 2 && unitsize != 4) ||
		(unitsize == 2 && (mask >> 16)) || state->used >= atom)
		return 0;
	while (length) {
		size_t take = atom - state->used;
		if (take > length) take = length;
		memcpy(state->pending + state->used, src, take);
		state->used += take; src += take; length -= take;
		if (state->used != atom) continue;
		for (bit = 0; bit < 64; bit++) {
			uint32_t sample = 0;
			size_t offset = 0;
			for (ch = 0; ch < 32; ch++) {
				if (!(mask & (UINT32_C(1) << ch))) continue;
				if (state->pending[offset + bit / 8] & (1u << (bit % 8)))
					sample |= UINT32_C(1) << ch;
				offset += 8;
			}
			for (byte = 0; byte < unitsize; byte++)
				*dst++ = (uint8_t)(sample >> (8 * byte));
		}
		count += 64;
		state->used = 0;
	}
	return count;
}

/* DSView trigger.c: replicate the active nibble/byte across FPGA lanes. */
static inline uint16_t dsl_trigger_replicate(uint16_t value, int quarter, int half)
{
	if (quarter) return (uint16_t)((value & 0xf) * 0x1111u);
	if (half) return (uint16_t)((value & 0xff) * 0x0101u);
	return value;
}

/* DSView dslogic.c dev_open/config_set, not the obsolete FX2 raw register. */
static inline int dsl_threshold_code(double voltage, int max25, uint8_t *code)
{
	if (!isfinite(voltage) || voltage < 0.0 || voltage > 5.0) return 0;
	*code = (uint8_t)(voltage / 3.3 * (max25 ? 1.0 / 2.0 : 1.5 / 2.5) * 255);
	return 1;
}
#endif

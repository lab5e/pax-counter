#include "coap.h"
#include <stdint.h>
#include <stdio.h>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
// #include "bg95_at.h"
#include "fota.h"
#include "context.h"
#include "util.h"
// #include "serial.h"

#pragma message "TODO: Several TODOs in coap.cpp :)"


/* Values as per RFC 7252, section-3.1.
 *
 * Option Delta/Length: 4-bit unsigned integer. A value between 0 and
 * 12 indicates the Option Delta/Length.  Three values are reserved for
 * special constructs:
 * 13: An 8-bit unsigned integer precedes the Option Value and indicates
 *     the Option Delta/Length minus 13.
 * 14: A 16-bit unsigned integer in network byte order precedes the
 *     Option Value and indicates the Option Delta/Length minus 269.
 * 15: Reserved for future use.
 */
#define COAP_OPTION_NO_EXT 12 /* Option's Delta/Length without extended data */
#define COAP_OPTION_EXT_13 13
#define COAP_OPTION_EXT_14 14
#define COAP_OPTION_EXT_15 15
#define COAP_OPTION_EXT_269 269

// TODO: 
// 1) Extract serial number
// 2) Build version
// 3) Read COAP response
// 4) INT pin from BL654 (for triggering SPI transaction)

/* CoAP Payload Marker */
#define COAP_MARKER		0xFF
#define BASIC_HEADER_SIZE	4

#define SET_BLOCK_SIZE(v, b) (v |= ((b) & 0x07))
#define SET_MORE(v, m) ((v) |= (m) ? 0x08 : 0x00)
#define SET_NUM(v, n) ((v) |= ((n) << 4))

#define GET_BLOCK_SIZE(v) (((v) & 0x7))
#define GET_MORE(v) (!!((v) & 0x08))
#define GET_NUM(v) ((v) >> 4)

#define	MIN(a, b)	((a) < (b) ? a : b)

#define ENOENT 2 // Yeah, yeah...

#define BLOCK_SIZE COAP_BLOCK_256
#define BLOCK_BYTES 256
#define MAX_BUFFER_LEN 512


#define MAX_RECEIVE_DATA_LIMIT 1024

char CURRENT_COAP_BUFFER[MAX_BUFFER_LEN] = {0};

// This is the general buffer used by the FOTA
static uint8_t request_buffer[MAX_BUFFER_LEN];
static uint8_t response_buffer[MAX_BUFFER_LEN];

int sys_csrand_get	(void * dst, size_t len)
{
    // TODO: ESP32 equivalent
	// int fd = open("/dev/random", O_RDONLY);
	// read(fd, dst, len);
	// close(fd);
	return 0;
}

/**
 *  @brief Put a 16-bit integer as big-endian to arbitrary location.
 *
 *  Put a 16-bit integer, originally in host endianness, to a
 *  potentially unaligned memory location in big-endian format.
 *
 *  @param val 16-bit integer in host endianness.
 *  @param dst Destination memory address to store the result.
 */
static inline void sys_put_be16(uint16_t val, uint8_t dst[2])
{
	dst[0] = val >> 8;
	dst[1] = val;
}

/**
 *  @brief Put a 32-bit integer as big-endian to arbitrary location.
 *
 *  Put a 32-bit integer, originally in host endianness, to a
 *  potentially unaligned memory location in big-endian format.
 *
 *  @param val 32-bit integer in host endianness.
 *  @param dst Destination memory address to store the result.
 */
static inline void sys_put_be32(uint32_t val, uint8_t dst[4])
{
	sys_put_be16(val >> 16, dst);
	sys_put_be16(val, &dst[2]);
}

static int fota_encode_simple_report(uint8_t *buffer, size_t *len)
{
	size_t sz = encode_tlv_string(buffer, FIRMWARE_VER_ID, (const uint8_t *)CLIENT_FIRMWARE_VER);
	sz += encode_tlv_string(buffer + sz, CLIENT_MANUFACTURER_ID, (const uint8_t *)CLIENT_MANUFACTURER);
	sz += encode_tlv_string(buffer + sz, SERIAL_NUMBER_ID, (const uint8_t *)CLIENT_SERIAL_NUMBER);
	sz += encode_tlv_string(buffer + sz, MODEL_NUMBER_ID, (const uint8_t *)CLIENT_MODEL_NUMBER);
	*len = sz;
	return 0;
}

 
// Any similarities to the COAP implementation in zephyr is purely coincidental...

// The CoAP message ID that is incremented each time coap_next_id() is called.
static uint16_t message_id;

uint16_t coap_next_id(void)
{
	return message_id++;
}

static inline bool append_u8(struct coap_packet *cpkt, uint8_t data)
{
	if (!cpkt) {
		return false;
	}

	if (cpkt->max_len - cpkt->offset < 1) {
		return false;
	}

	cpkt->data[cpkt->offset++] = data;

	return true;
}

static inline bool append_be16(struct coap_packet *cpkt, uint16_t data)
{
	if (!cpkt) {
		return false;
	}

	if (cpkt->max_len - cpkt->offset < 2) {
		return false;
	}

	cpkt->data[cpkt->offset++] = data >> 8;
	cpkt->data[cpkt->offset++] = (uint8_t) data;

	return true;
}

static inline bool append(struct coap_packet *cpkt, const uint8_t *data, uint16_t len)
{
	if (!cpkt || !data) {
		return false;
	}

	if (cpkt->max_len - cpkt->offset < len) {
		return false;
	}

	memcpy(cpkt->data + cpkt->offset, data, len);
	cpkt->offset += len;

	return true;
}

int coap_packet_init(struct coap_packet *cpkt, uint8_t *data, uint16_t max_len,
		     uint8_t ver, uint8_t type, uint8_t token_len,
		     const uint8_t *token, uint8_t code, uint16_t id)
{
	uint8_t hdr;
	bool res;

	if (!cpkt || !data || !max_len) {
		return -EINVAL;
	}

	memset(cpkt, 0, sizeof(*cpkt));

	cpkt->data = data;
	cpkt->offset = 0U;
	cpkt->max_len = max_len;
	cpkt->delta = 0U;

	hdr = (ver & 0x3) << 6;
	hdr |= (type & 0x3) << 4;
	hdr |= token_len & 0xF;

	res = append_u8(cpkt, hdr);
	if (!res) {
		return -EINVAL;
	}

	res = append_u8(cpkt, code);
	if (!res) {
		return -EINVAL;
	}

	res = append_be16(cpkt, id);
	if (!res) {
		return -EINVAL;
	}

	if (token && token_len) {
		res = append(cpkt, token, token_len);
		if (!res) {
			return -EINVAL;
		}
	}

	/* Header length : (version + type + tkl) + code + id + [token] */
	cpkt->hdr_len = 1 + 1 + 2 + token_len;

	return 0;
}

static void option_header_set_delta(uint8_t *opt, uint8_t delta)
{
	*opt = (delta & 0xF) << 4;
}

static void option_header_set_len(uint8_t *opt, uint8_t len)
{
	*opt |= (len & 0xF);
}


static uint8_t encode_extended_option(uint16_t num, uint8_t *opt, uint16_t *ext)
{
	if (num < COAP_OPTION_EXT_13) {
		*opt = num;
		*ext = 0U;

		return 0;
	} else if (num < COAP_OPTION_EXT_269) {
		*opt = COAP_OPTION_EXT_13;
		*ext = num - COAP_OPTION_EXT_13;

		return 1;
	}

	*opt = COAP_OPTION_EXT_14;
	*ext = num - COAP_OPTION_EXT_269;

	return 2;
}

static int encode_option(struct coap_packet *cpkt, uint16_t code,
			 const uint8_t *value, uint16_t len)
{
	uint16_t delta_ext; /* Extended delta */
	uint16_t len_ext; /* Extended length */
	uint8_t opt; /* delta | len */
	uint8_t opt_delta;
	uint8_t opt_len;
	uint8_t delta_size;
	uint8_t len_size;
	bool res;

	delta_size = encode_extended_option(code, &opt_delta, &delta_ext);
	len_size = encode_extended_option(len, &opt_len, &len_ext);

	option_header_set_delta(&opt, opt_delta);
	option_header_set_len(&opt, opt_len);

	res = append_u8(cpkt, opt);
	if (!res) {
		return -EINVAL;
	}

	if (delta_size == 1U) {
		res = append_u8(cpkt, (uint8_t)delta_ext);
		if (!res) {
			return -EINVAL;
		}
	} else if (delta_size == 2U) {
		res = append_be16(cpkt, delta_ext);
		if (!res) {
			return -EINVAL;
		}
	}

	if (len_size == 1U) {
		res = append_u8(cpkt, (uint8_t)len_ext);
		if (!res) {
			return -EINVAL;
		}
	} else if (len_size == 2U) {
		res = append_be16(cpkt, len_ext);
		if (!res) {
			return -EINVAL;
		}
	}

	if (len && value) {
		res = append(cpkt, value, len);
		if (!res) {
			return -EINVAL;
		}
	}

	return  (1 + delta_size + len_size + len);
}



/* TODO Add support for inserting options in proper place
 * and modify other option's delta accordingly.
 */
int coap_packet_append_option(struct coap_packet *cpkt, uint16_t code,
			      const uint8_t *value, uint16_t len)
{
	int r;

	if (!cpkt) {
		return -EINVAL;
	}

	if (len && !value) {
		return -EINVAL;
	}

	if (code < cpkt->delta) {
		printf("ERROR: Options should be in ascending order\n");
		return -EINVAL;
	}

	/* Calculate delta, if this option is not the first one */
	if (cpkt->opt_len) {
		code = (code == cpkt->delta) ? 0 : code - cpkt->delta;
	}

	r = encode_option(cpkt, code, value, len);
	if (r < 0) {
		return -EINVAL;
	}

	cpkt->opt_len += r;
	cpkt->delta += code;

	return 0;
}

int coap_append_option_int(struct coap_packet *cpkt, uint16_t code,
			   unsigned int val)
{
	uint8_t data[4], len;

	if (val == 0U) {
		data[0] = 0U;
		len = 0U;
	} else if (val < 0xFF) {
		data[0] = (uint8_t) val;
		len = 1U;
	} else if (val < 0xFFFF) {
		sys_put_be16(val, data);
		len = 2U;
	} else if (val < 0xFFFFFF) {
		sys_put_be16(val, &data[1]);
		data[0] = val >> 16;
		len = 3U;
	} else {
		sys_put_be32(val, data);
		len = 4U;
	}

	return coap_packet_append_option(cpkt, code, data, len);
}

unsigned int coap_option_value_to_int(const struct coap_option *option)
{
	switch (option->len) {
	case 0:
		return 0;
	case 1:
		return option->value[0];
	case 2:
		return (option->value[1] << 0) | (option->value[0] << 8);
	case 3:
		return (option->value[2] << 0) | (option->value[1] << 8) |
			(option->value[0] << 16);
	case 4:
		return (option->value[3] << 0) | (option->value[2] << 8) |
			(option->value[1] << 16) | (option->value[0] << 24);
	default:
		return 0;
	}

	return 0;
}

int coap_packet_append_payload_marker(struct coap_packet *cpkt)
{
	return append_u8(cpkt, COAP_MARKER) ? 0 : -EINVAL;
}

int coap_packet_append_payload(struct coap_packet *cpkt, const uint8_t *payload,
			       uint16_t payload_len)
{
	return append(cpkt, payload, payload_len) ? 0 : -EINVAL;
}

// Not exactly the zephyr version, but I guess it will do for now...
int sys_rand32_get()
{
	return rand();
}

uint8_t *coap_next_token(void)
{
	static uint32_t rand[
		ceiling_fraction(COAP_TOKEN_MAX_LEN, sizeof(uint32_t))];

	for (size_t i = 0; i < ARRAY_SIZE(rand); ++i) {
		rand[i] = sys_rand32_get();
	}

	return (uint8_t *) rand;
}

static int read_u8(uint8_t *data, uint16_t offset, uint16_t *pos,
		   uint16_t max_len, uint8_t *value)
{
	if (max_len - offset < 1) {
		return -EINVAL;
	}

	*value = data[offset++];
	*pos = offset;

	return max_len - offset;
}

static int read_be16(uint8_t *data, uint16_t offset, uint16_t *pos,
		     uint16_t max_len, uint16_t *value)
{
	if (max_len - offset < 2) {
		return -EINVAL;
	}

	*value = data[offset++] << 8;
	*value |= data[offset++];
	*pos = offset;

	return max_len - offset;
}

static uint8_t option_header_get_delta(uint8_t opt)
{
	return (opt & 0xF0) >> 4;
}

static uint8_t option_header_get_len(uint8_t opt)
{
	return opt & 0x0F;
}

static inline bool u16_add_overflow(uint16_t a, uint16_t b, uint16_t *result)
{
	uint16_t c = a + b;

	*result = c;

	return c < a;
}

static int decode_delta(uint8_t *data, uint16_t offset, uint16_t *pos, uint16_t max_len,
			uint16_t opt, uint16_t *opt_ext, uint16_t *hdr_len)
{
	int ret = 0;

	if (opt == COAP_OPTION_EXT_13) {
		uint8_t val;

		*hdr_len = 1U;

		ret = read_u8(data, offset, pos, max_len, &val);
		if (ret < 0) {
			return -EINVAL;
		}

		opt = val + COAP_OPTION_EXT_13;
	} else if (opt == COAP_OPTION_EXT_14) {
		uint16_t val;

		*hdr_len = 2U;

		ret = read_be16(data, offset, pos, max_len, &val);
		if (ret < 0) {
			return -EINVAL;
		}

		opt = val + COAP_OPTION_EXT_269;
	} else if (opt == COAP_OPTION_EXT_15) {
		return -EINVAL;
	}

	*opt_ext = opt;

	return ret;
}

static int read(uint8_t *data, uint16_t offset, uint16_t *pos,
		uint16_t max_len, uint16_t len, uint8_t *value)
{
	if (max_len - offset < len) {
		return -EINVAL;
	}

	memcpy(value, data + offset, len);
	offset += len;
	*pos = offset;

	return max_len - offset;
}

static int parse_option(uint8_t *data, uint16_t offset, uint16_t *pos,
			uint16_t max_len, uint16_t *opt_delta, uint16_t *opt_len,
			struct coap_option *option)
{
	uint16_t hdr_len;
	uint16_t delta;
	uint16_t len;
	uint8_t opt;
	int r;

	r = read_u8(data, offset, pos, max_len, &opt);
	if (r < 0) {
		return r;
	}

	*opt_len += 1U;

	/* This indicates that options have ended */
	if (opt == COAP_MARKER) {
		/* packet w/ marker but no payload is malformed */
		return r > 0 ? 0 : -EINVAL;
	}

	delta = option_header_get_delta(opt);
	len = option_header_get_len(opt);

	/* r == 0 means no more data to read from fragment, but delta
	 * field shows that packet should contain more data, it must
	 * be a malformed packet.
	 */
	if (r == 0 && delta > COAP_OPTION_NO_EXT) {
		return -EINVAL;
	}

	if (delta > COAP_OPTION_NO_EXT) {
		/* In case 'delta' doesn't fit the option fixed header. */
		r = decode_delta(data, *pos, pos, max_len,
				 delta, &delta, &hdr_len);
		if ((r < 0) || (r == 0 && len > COAP_OPTION_NO_EXT)) {
			return -EINVAL;
		}

		if (u16_add_overflow(*opt_len, hdr_len, opt_len)) {
			return -EINVAL;
		}
	}

	if (len > COAP_OPTION_NO_EXT) {
		/* In case 'len' doesn't fit the option fixed header. */
		r = decode_delta(data, *pos, pos, max_len,
				 len, &len, &hdr_len);
		if (r < 0) {
			return -EINVAL;
		}

		if (u16_add_overflow(*opt_len, hdr_len, opt_len)) {
			return -EINVAL;
		}
	}

	if (u16_add_overflow(*opt_delta, delta, opt_delta) ||
	    u16_add_overflow(*opt_len, len, opt_len)) {
		return -EINVAL;
	}

	if (r == 0 && len != 0U) {
		/* r == 0 means no more data to read from fragment, but len
		 * field shows that packet should contain more data, it must
		 * be a malformed packet.
		 */
		return -EINVAL;
	}

	if (option) {
		/*
		 * Make sure the option data will fit into the value field of
		 * coap_option.
		 * NOTE: To expand the size of the value field set:
		 * CONFIG_COAP_EXTENDED_OPTIONS_LEN=y
		 * CONFIG_COAP_EXTENDED_OPTIONS_LEN_VALUE=<size>
		 */
		if (len > sizeof(option->value)) {
			printf("ERROR: %u is > sizeof(coap_option->value)(%zu)!",
				len, sizeof(option->value));
			return -EINVAL;
		}

		option->delta = *opt_delta;
		option->len = len;
		r = read(data, *pos, pos, max_len, len, &option->value[0]);
		if (r < 0) {
			return -EINVAL;
		}
	} else {
		if (u16_add_overflow(*pos, len, pos)) {
			return -EINVAL;
		}

		r = max_len - *pos;
	}

	return r;
}

int coap_packet_parse(struct coap_packet *cpkt, uint8_t *data, uint16_t len,
		      struct coap_option *options, uint8_t opt_num)
{
	uint16_t opt_len;
	uint16_t offset;
	uint16_t delta;
	uint8_t num;
	uint8_t tkl;
	int ret;

	if (!cpkt || !data) {
		return -EINVAL;
	}

	if (len < BASIC_HEADER_SIZE) {
		return -EINVAL;
	}

	if (options) {
		memset(options, 0, opt_num * sizeof(struct coap_option));
	}

	cpkt->data = data;
	cpkt->offset = 0U;
	cpkt->max_len = len;
	cpkt->opt_len = 0U;
	cpkt->hdr_len = 0U;
	cpkt->delta = 0U;

	/* Token lengths 9-15 are reserved. */
	tkl = cpkt->data[0] & 0x0f;
	if (tkl > 8) {
		return -EINVAL;
	}

	cpkt->hdr_len = BASIC_HEADER_SIZE + tkl;
	if (cpkt->hdr_len > len) {
		return -EINVAL;
	}

	cpkt->offset = cpkt->hdr_len;
	if (cpkt->hdr_len == len) {
		return 0;
	}

	offset = cpkt->offset;
	opt_len = 0U;
	delta = 0U;
	num = 0U;

	while (1) {
		struct coap_option *option;

		option = num < opt_num ? &options[num++] : NULL;
		ret = parse_option(cpkt->data, offset, &offset, cpkt->max_len,
				   &delta, &opt_len, option);
		if (ret < 0) {
			return ret;
		} else if (ret == 0) {
			break;
		}
	}

	cpkt->opt_len = opt_len;
	cpkt->delta = delta;
	cpkt->offset = offset;

	return 0;
}

int coap_find_options(const struct coap_packet *cpkt, uint16_t code,
		      struct coap_option *options, uint16_t veclen)
{
	uint16_t opt_len;
	uint16_t offset;
	uint16_t delta;
	uint8_t num;
	int r;

	offset = cpkt->hdr_len;
	opt_len = 0U;
	delta = 0U;
	num = 0U;

	while (delta <= code && num < veclen) {
		r = parse_option(cpkt->data, offset, &offset,
				 cpkt->max_len, &delta, &opt_len,
				 &options[num]);
		if (r < 0) {
			return -EINVAL;
		}

		if (code == options[num].delta) {
			num++;
		}

		if (r == 0) {
			break;
		}
	}

	return num;
}


const uint8_t *coap_packet_get_payload(const struct coap_packet *cpkt, uint16_t *len)
{
	int payload_len;

	if (!cpkt || !len) {
		return NULL;
	}

	payload_len = cpkt->max_len - cpkt->hdr_len - cpkt->opt_len;
	if (payload_len > 0) {
		*len = payload_len;
	} else {
		*len = 0U;
	}

	return !(*len) ? NULL :
		cpkt->data + cpkt->hdr_len + cpkt->opt_len;
}


static int fota_decode_simple_response(simple_fota_response_t *resp, const uint8_t *buf, size_t len)
{
	size_t idx = 0;
	int err = 0;
	while (idx < len)
	{
		uint8_t id = buf[idx++];
		switch (id)
		{
		case HOST_ID:
			err = decode_tlv_string(buf, &idx, resp->host);
			if (err)
			{
				return err;
			}
			break;
		case PORT_ID:
			err = decode_tlv_uint32(buf, &idx, &resp->port);
			if (err)
			{
				return err;
			}
			break;
		case PATH_ID:
			err = decode_tlv_string(buf, &idx, resp->path);
			if (err)
			{
				return err;
			}
			break;
		case AVAILABLE_ID:
			err = decode_tlv_bool(buf, &idx, &resp->scheduled_update);
			if (err)
			{
				return err;
			}
			break;
		default:
			printf("ERROR: Unknown field id in FOTA response: %d\n", id);
			return -1;
		}
	}
	return 0;
}

static uint8_t __coap_header_get_code(const struct coap_packet *cpkt)
{
	if (!cpkt || !cpkt->data) {
		return 0;
	}

	return cpkt->data[1];
}


// Helper for converting the enumeration to the size expressed
// in bytes.
//
// @param block_size The block size to be converted
//
// @return The size in bytes that the block_size represents
//
static inline uint16_t coap_block_size_to_bytes(
	enum coap_block_size block_size)
{
	return (1 << (block_size + 4));
}

uint8_t coap_header_get_code(const struct coap_packet *cpkt)
{
	uint8_t code = __coap_header_get_code(cpkt);

	switch (code) {
	/* Methods are encoded in the code field too */
	case COAP_METHOD_GET:
	case COAP_METHOD_POST:
	case COAP_METHOD_PUT:
	case COAP_METHOD_DELETE:

	/* All the defined response codes */
	case COAP_RESPONSE_CODE_OK:
	case COAP_RESPONSE_CODE_CREATED:
	case COAP_RESPONSE_CODE_DELETED:
	case COAP_RESPONSE_CODE_VALID:
	case COAP_RESPONSE_CODE_CHANGED:
	case COAP_RESPONSE_CODE_CONTENT:
	case COAP_RESPONSE_CODE_CONTINUE:
	case COAP_RESPONSE_CODE_BAD_REQUEST:
	case COAP_RESPONSE_CODE_UNAUTHORIZED:
	case COAP_RESPONSE_CODE_BAD_OPTION:
	case COAP_RESPONSE_CODE_FORBIDDEN:
	case COAP_RESPONSE_CODE_NOT_FOUND:
	case COAP_RESPONSE_CODE_NOT_ALLOWED:
	case COAP_RESPONSE_CODE_NOT_ACCEPTABLE:
	case COAP_RESPONSE_CODE_INCOMPLETE:
	case COAP_RESPONSE_CODE_PRECONDITION_FAILED:
	case COAP_RESPONSE_CODE_REQUEST_TOO_LARGE:
	case COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT:
	case COAP_RESPONSE_CODE_INTERNAL_ERROR:
	case COAP_RESPONSE_CODE_NOT_IMPLEMENTED:
	case COAP_RESPONSE_CODE_BAD_GATEWAY:
	case COAP_RESPONSE_CODE_SERVICE_UNAVAILABLE:
	case COAP_RESPONSE_CODE_GATEWAY_TIMEOUT:
	case COAP_RESPONSE_CODE_PROXYING_NOT_SUPPORTED:
	case COAP_CODE_EMPTY:
		return code;
	default:
		return COAP_CODE_EMPTY;
	}
}

static bool is_request(const struct coap_packet *cpkt)
{
	uint8_t code = coap_header_get_code(cpkt);

	return !(code & ~COAP_REQUEST_MASK);
}

int coap_append_block2_option(struct coap_packet *cpkt,
			      struct coap_block_context *ctx)
{
	int r, val = 0;
	uint16_t bytes = coap_block_size_to_bytes(ctx->block_size);

	if (is_request(cpkt)) {
		SET_BLOCK_SIZE(val, ctx->block_size);
		SET_NUM(val, ctx->current / bytes);
	} else {
		SET_BLOCK_SIZE(val, ctx->block_size);
		SET_MORE(val, ctx->current + bytes < ctx->total_size);
		SET_NUM(val, ctx->current / bytes);
	}

	r = coap_append_option_int(cpkt, COAP_OPTION_BLOCK2, val);

	return r;
}

int coap_block_transfer_init(struct coap_block_context *ctx,
			      enum coap_block_size block_size,
			      size_t total_size)
{
	ctx->block_size = block_size;
	ctx->total_size = total_size;
	ctx->current = 0;

	return 0;
}

void close_comms(int context_id, int socket_id)
{
    // TODO
}


int fota_report_version(simple_fota_response_t *resp)
{
	struct coap_packet p;
	int err = 0;
#define TOKEN_SIZE 8
	uint8_t token[TOKEN_SIZE];
	sys_csrand_get(token, TOKEN_SIZE);

	if (coap_packet_init(&p, request_buffer, sizeof(request_buffer), 1, COAP_TYPE_CON,
						 TOKEN_SIZE, token, COAP_METHOD_POST,
						 coap_next_id()) < 0)
	{
		printf("ERROR: Unable to iniitialize CoAP packet\n");
		return -1;
	}

	if (coap_packet_append_option(&p, COAP_OPTION_URI_PATH,
								  (const uint8_t*)DEFAULT_FOTA_COAP_REPORT_PATH,
								  strlen(DEFAULT_FOTA_COAP_REPORT_PATH)) < 0)
	{
		printf("ERROR: Could not append path option to packet\n");
		return -1;
	}

	if (coap_packet_append_payload_marker(&p) < 0)
	{
		printf("ERROR: Unable to append payload marker to packet\n");
		return -1;
	}

	const int TMP_BUFFER_LENGTH = 256;
	uint8_t tmp_buffer[TMP_BUFFER_LENGTH];
	size_t len;
	if (fota_encode_simple_report(tmp_buffer, &len) < 0)
	{
		printf("ERROR: Unable to encode FOTA report\n");
		return -1;
	}
	if (coap_packet_append_payload(&p, tmp_buffer, len) < 0)
	{
		printf("ERROR: Unable to append payload to CoAP packet\n");
		return -1;
	}

	int ret_code;
	char hex_encoded_transmit_buffer[256];
	uint8_t udp_receive_buffer[MAX_RECEIVE_DATA_LIMIT];
	hex_encode_buffer(request_buffer, p.offset, hex_encoded_transmit_buffer);
	int socket_id = 1;

	int received = 0;
    // TODO
    // ret_code = open_comms(LAB5E_CONTEXT, socket_id, SPAN_ADDRESS, DEFAULT_FOTA_COAP_PORT);
    // ret_code = send_udp_hex_encoded_wait_for_urc_recv(socket_id, hex_encoded_transmit_buffer);
    // received = read_udp(socket_id, udp_receive_buffer);

	struct coap_packet reply;
	if (coap_packet_parse(&reply, udp_receive_buffer, received, NULL, 0) < 0)
	{
		printf("ERROR: Could not parse CoAP reply from server\n");
		close_comms(LAB5E_CONTEXT, socket_id);
		return -1;
	}

	uint16_t payload_len = 0;
	const uint8_t *buf = coap_packet_get_payload(&reply, &payload_len);

	printf("Decode response (%d bytes)\n", payload_len);
	if (fota_decode_simple_response(resp, buf, payload_len) < 0)
	{
		printf("ERROR: Could not decode response from server.\n");
		close_comms(LAB5E_CONTEXT, socket_id);
		return -1;
	}
	printf("FOTA host: %s\n", resp->host);
	printf("FOTA port: %d\n", resp->port);
	printf("FOTA path: %s\n", resp->path);
	printf("FOTA update: %s\n", resp->scheduled_update ? "yes": "no");

	close_comms(LAB5E_CONTEXT, socket_id);

	return 0;
}

int coap_get_option_int(const struct coap_packet *cpkt, uint16_t code)
{
	struct coap_option option = {};
	unsigned int val;
	int count = 1;

	count = coap_find_options(cpkt, code, &option, count);
	if (count <= 0) {
		return -ENOENT;
	}

	val = coap_option_value_to_int(&option);

	return val;
}

static int update_descriptive_block(struct coap_block_context *ctx,
				    int block, int size)
{
	size_t new_current = GET_NUM(block) << (GET_BLOCK_SIZE(block) + 4);

	if (block == -ENOENT) {
		return 0;
	}

	if (size && ctx->total_size && ctx->total_size != size) {
		return -EINVAL;
	}

	if (ctx->current > 0 && GET_BLOCK_SIZE(block) > ctx->block_size) {
		return -EINVAL;
	}

	if (ctx->total_size && new_current > ctx->total_size) {
		return -EINVAL;
	}

	if (size) {
		ctx->total_size = size;
	}
	ctx->current = new_current;
	ctx->block_size = (coap_block_size)MIN(GET_BLOCK_SIZE(block), ctx->block_size);

	return 0;
}

static int update_control_block1(struct coap_block_context *ctx,
				     int block, int size)
{
	size_t new_current = GET_NUM(block) << (GET_BLOCK_SIZE(block) + 4);

	if (block == -ENOENT) {
		return 0;
	}

	if (new_current != ctx->current) {
		return -EINVAL;
	}

	if (GET_BLOCK_SIZE(block) > ctx->block_size) {
		return -EINVAL;
	}

	ctx->block_size = (coap_block_size)GET_BLOCK_SIZE(block);
	ctx->total_size = size;

	return 0;
}

static int update_control_block2(struct coap_block_context *ctx,
				 int block, int size)
{
	size_t new_current = GET_NUM(block) << (GET_BLOCK_SIZE(block) + 4);

	if (block == -ENOENT) {
		return 0;
	}

	if (GET_MORE(block)) {
		return -EINVAL;
	}

	if (GET_NUM(block) > 0 && GET_BLOCK_SIZE(block) != ctx->block_size) {
		return -EINVAL;
	}

	ctx->current = new_current;
	ctx->block_size = (coap_block_size)MIN(GET_BLOCK_SIZE(block), ctx->block_size);

	return 0;
}

int coap_update_from_block(const struct coap_packet *cpkt,
			   struct coap_block_context *ctx)
{
	int r, block1, block2, size1, size2;

	block1 = coap_get_option_int(cpkt, COAP_OPTION_BLOCK1);
	block2 = coap_get_option_int(cpkt, COAP_OPTION_BLOCK2);
	size1 = coap_get_option_int(cpkt, COAP_OPTION_SIZE1);
	size2 = coap_get_option_int(cpkt, COAP_OPTION_SIZE2);

	size1 = size1 == -ENOENT ? 0 : size1;
	size2 = size2 == -ENOENT ? 0 : size2;

	if (is_request(cpkt)) {
		r = update_control_block2(ctx, block2, size2);
		if (r) {
			return r;
		}

		return update_descriptive_block(ctx, block1, size1);
	}

	r = update_control_block1(ctx, block1, size1);
	if (r) {
		return r;
	}

	return update_descriptive_block(ctx, block2, size2);
}

size_t coap_next_block(const struct coap_packet *cpkt,
		       struct coap_block_context *ctx)
{
	int block;

	if (is_request(cpkt)) {
		block = coap_get_option_int(cpkt, COAP_OPTION_BLOCK1);
	} else {
		block = coap_get_option_int(cpkt, COAP_OPTION_BLOCK2);
	}

	if (!GET_MORE(block)) {
		return 0;
	}

	ctx->current += coap_block_size_to_bytes(ctx->block_size);

	return ctx->current;
}

static bool fota_download_image(simple_fota_response_t *resp)
{
	printf("Downloading new firmware image...\n");
	struct coap_block_context block_ctx;
	memset(&block_ctx, 0, sizeof(block_ctx));

	coap_block_transfer_init(&block_ctx, BLOCK_SIZE, 0);

	int socket_id = 1;
	int ret_code;

    // TODO
	// ret_code = open_comms(LAB5E_CONTEXT, socket_id, resp->host, resp->port);

	bool last_block = false;
	const uint8_t token_length = 8;
	uint8_t token[token_length];
	size_t total_size = 0;
	memcpy(token, coap_next_token(), token_length);


	while (!last_block)
	{
		struct coap_packet request;
		memset(request_buffer, 0, MAX_BUFFER_LEN);
		if (coap_packet_init(&request, request_buffer, MAX_BUFFER_LEN, 1,
							 COAP_TYPE_CON, token_length, token,
							 COAP_METHOD_GET, coap_next_id()) < 0)
		{
			printf("ERROR: Could not init request packet\n");
			close_comms(LAB5E_CONTEXT, socket_id);
			return false;
		}

		// Assuming a single path entry here. It might be more.
		if (coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
									  (const uint8_t *)resp->path, strlen(resp->path)) < 0)
		{
			printf("ERROR: Could not init path option\n");
			close_comms(LAB5E_CONTEXT, socket_id);
			return false;
		}
		if (coap_append_block2_option(&request, &block_ctx) < 0)
		{
			printf("ERROR: Could not append block option");
			close_comms(LAB5E_CONTEXT, socket_id);
			return false;
		}

		char hex_encoded_transmit_buffer[MAX_BUFFER_LEN];
		uint8_t udp_receive_buffer[MAX_RECEIVE_DATA_LIMIT];
		hex_encode_buffer(request_buffer, request.offset, hex_encoded_transmit_buffer);

        // TODO
		// ret_code = send_udp_hex_encoded_wait_for_urc_recv(socket_id, hex_encoded_transmit_buffer);

		if (ret_code < 0)
		{
			close_comms(LAB5E_CONTEXT, socket_id);
			printf("ERROR: Error sending %d bytes on socket\n", request.offset);
			return false;
		}

		int received = 0;
        // TODO
        // received = bg95_read_udp(socket_id, udp_receive_buffer);

		struct coap_packet reply;

		if (coap_packet_parse(&reply, udp_receive_buffer, received, NULL, 0) < 0)
		{
			close_comms(LAB5E_CONTEXT, socket_id);
			printf("ERROR: Invalid data received\n");
			return false;
		}

		if (coap_update_from_block(&reply, &block_ctx) < 0)
		{
			printf("ERROR: Error updating from block\n");
			close_comms(LAB5E_CONTEXT, socket_id);
			return false;
		}
		last_block = !coap_next_block(&reply, &block_ctx);
		uint16_t payload_len = 0;
		const uint8_t *payload = coap_packet_get_payload(&reply, &payload_len);
		total_size += payload_len;
		printf("Retreived block %d (%d of %d bytes) \n", block_ctx.current / BLOCK_BYTES, total_size, block_ctx.total_size);

		bool first = total_size == payload_len;
		bool last = total_size == block_ctx.total_size;

/*		
		// TODO: Write firmware to disk
		if (write_firmware_block(payload, payload_len, first, last, block_ctx.total_size) < 0)
		{
			LOG_ERR("Error writing firmware block");
			close(sock);
			return false;
		}
*/		
	}
	close_comms(LAB5E_CONTEXT, socket_id);

	return true;
}


void test_coap()
{
    printf("---------\n");
    printf("COAP Test\n");

	simple_fota_response_t resp;

	int ret = fota_report_version(&resp);
	if (ret < 0)
		printf("ERROR: fota_report_version failed\n");

	if (resp.scheduled_update)
	{
		printf("TBW Blockwise transfer...\n");
		fota_download_image(&resp);
	}

}
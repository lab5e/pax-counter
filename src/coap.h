#ifndef _COAP_H_
#define _COAP_H_

#include <stdint.h>
#include <stddef.h>

// Default COAP parameters
#define DEFAULT_FOTA_COAP_REPORT_PATH   "u"
#define DEFAULT_FOTA_COAP_UPDATE_PATH   "fw"
#define DEFAULT_FOTA_COAP_PORT 5683


#define EINVAL   22

#define COAP_HEADER_VERSION(data)  ( (0xC0 & data[0])>>6    )
#define COAP_HEADER_TYPE(data)     ( (0x30 & data[0])>>4    )
#define COAP_HEADER_TKL(data)      ( (0x0F & data[0])>>0    )
#define COAP_HEADER_CLASS(data)    ( ((data[1]>>5)&0x07)    )
#define COAP_HEADER_CODE(data)     ( ((data[1]>>0)&0x1F)    )
#define COAP_HEADER_MID(data)      ( (data[2]<<8)|(data[3]) )

typedef struct
{
	char host[25];
	uint32_t port;
	char path[25];
	bool scheduled_update;
} simple_fota_response_t;

enum coap_method {
	COAP_METHOD_GET = 1,
	COAP_METHOD_POST = 2,
	COAP_METHOD_PUT = 3,
	COAP_METHOD_DELETE = 4,
};

#define COAP_REQUEST_MASK 0x07

struct coap_option {
	uint16_t delta;
#if defined(CONFIG_COAP_EXTENDED_OPTIONS_LEN)
	uint16_t len;
	uint8_t value[CONFIG_COAP_EXTENDED_OPTIONS_LEN_VALUE];
#else
	uint8_t len;
	uint8_t value[12];
#endif
};

enum coap_msgtype {
	/**
	 * Confirmable message.
	 *
	 * The packet is a request or response the destination end-point must
	 * acknowledge.
	 */
	COAP_TYPE_CON = 0,
	/**
	 * Non-confirmable message.
	 *
	 * The packet is a request or response that doesn't
	 * require acknowledgements.
	 */
	COAP_TYPE_NON_CON = 1,
	/**
	 * Acknowledge.
	 *
	 * Response to a confirmable message.
	 */
	COAP_TYPE_ACK = 2,
	/**
	 * Reset.
	 *
	 * Rejecting a packet for any reason is done by sending a message
	 * of this type.
	 */
	COAP_TYPE_RESET = 3
};

/**
 * @brief Set of CoAP packet options we are aware of.
 *
 * Users may add options other than these to their packets, provided
 * they know how to format them correctly. The only restriction is
 * that all options must be added to a packet in numeric order.
 *
 * Refer to RFC 7252, section 12.2 for more information.
 */
enum coap_option_num {
	COAP_OPTION_IF_MATCH = 1,
	COAP_OPTION_URI_HOST = 3,
	COAP_OPTION_ETAG = 4,
	COAP_OPTION_IF_NONE_MATCH = 5,
	COAP_OPTION_OBSERVE = 6,
	COAP_OPTION_URI_PORT = 7,
	COAP_OPTION_LOCATION_PATH = 8,
	COAP_OPTION_URI_PATH = 11,
	COAP_OPTION_CONTENT_FORMAT = 12,
	COAP_OPTION_MAX_AGE = 14,
	COAP_OPTION_URI_QUERY = 15,
	COAP_OPTION_ACCEPT = 17,
	COAP_OPTION_LOCATION_QUERY = 20,
	COAP_OPTION_BLOCK2 = 23,
	COAP_OPTION_BLOCK1 = 27,
	COAP_OPTION_SIZE2 = 28,
	COAP_OPTION_PROXY_URI = 35,
	COAP_OPTION_PROXY_SCHEME = 39,
	COAP_OPTION_SIZE1 = 60,
};

struct coap_packet 
{
	uint8_t *data;      // User allocated buffer 
	uint16_t offset;    // CoAP lib maintains offset while adding data 
	uint16_t max_len;   // Max CoAP packet data length 
	uint8_t hdr_len;    // CoAP header length 
	uint16_t opt_len;   // Total options length (delta + len + value) 
	uint16_t delta;     // Used for delta calculation in CoAP packet 
};

enum coap_block_size {
	COAP_BLOCK_16,
	COAP_BLOCK_32,
	COAP_BLOCK_64,
	COAP_BLOCK_128,
	COAP_BLOCK_256,
	COAP_BLOCK_512,
	COAP_BLOCK_1024,
};

// Represents the current state of a block-wise transaction.
struct coap_block_context {
	size_t total_size;
	size_t current;
	enum coap_block_size block_size;
};

#define coap_make_response_code(class, det) ((class << 5) | (det))


//  Set of response codes available for a response packet.
// 
//  To be used when creating a response.
// 
enum coap_response_code {
	COAP_RESPONSE_CODE_OK = coap_make_response_code(2, 0),
	COAP_RESPONSE_CODE_CREATED = coap_make_response_code(2, 1),
	COAP_RESPONSE_CODE_DELETED = coap_make_response_code(2, 2),
	COAP_RESPONSE_CODE_VALID = coap_make_response_code(2, 3),
	COAP_RESPONSE_CODE_CHANGED = coap_make_response_code(2, 4),
	COAP_RESPONSE_CODE_CONTENT = coap_make_response_code(2, 5),
	COAP_RESPONSE_CODE_CONTINUE = coap_make_response_code(2, 31),
	COAP_RESPONSE_CODE_BAD_REQUEST = coap_make_response_code(4, 0),
	COAP_RESPONSE_CODE_UNAUTHORIZED = coap_make_response_code(4, 1),
	COAP_RESPONSE_CODE_BAD_OPTION = coap_make_response_code(4, 2),
	COAP_RESPONSE_CODE_FORBIDDEN = coap_make_response_code(4, 3),
	COAP_RESPONSE_CODE_NOT_FOUND = coap_make_response_code(4, 4),
	COAP_RESPONSE_CODE_NOT_ALLOWED = coap_make_response_code(4, 5),
	COAP_RESPONSE_CODE_NOT_ACCEPTABLE = coap_make_response_code(4, 6),
	COAP_RESPONSE_CODE_INCOMPLETE = coap_make_response_code(4, 8),
	COAP_RESPONSE_CODE_PRECONDITION_FAILED = coap_make_response_code(4, 12),
	COAP_RESPONSE_CODE_REQUEST_TOO_LARGE = coap_make_response_code(4, 13),
	COAP_RESPONSE_CODE_UNSUPPORTED_CONTENT_FORMAT =
						coap_make_response_code(4, 15),
	COAP_RESPONSE_CODE_INTERNAL_ERROR = coap_make_response_code(5, 0),
	COAP_RESPONSE_CODE_NOT_IMPLEMENTED = coap_make_response_code(5, 1),
	COAP_RESPONSE_CODE_BAD_GATEWAY = coap_make_response_code(5, 2),
	COAP_RESPONSE_CODE_SERVICE_UNAVAILABLE = coap_make_response_code(5, 3),
	COAP_RESPONSE_CODE_GATEWAY_TIMEOUT = coap_make_response_code(5, 4),
	COAP_RESPONSE_CODE_PROXYING_NOT_SUPPORTED =
						coap_make_response_code(5, 5)
};

#define COAP_CODE_EMPTY (0)
#define COAP_TOKEN_MAX_LEN 8UL

#define ceiling_fraction(numerator, divider) \
	(((numerator) + ((divider) - 1)) / (divider))

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

int sys_csrand_get	(void * dst, size_t len);

int coap_packet_init(struct coap_packet *cpkt, uint8_t *data, uint16_t max_len,
		     uint8_t ver, uint8_t type, uint8_t token_len,
		     const uint8_t *token, uint8_t code, uint16_t id);

int fota_report_version(simple_fota_response_t *resp);



void test_coap();

#endif // _COAP_H_
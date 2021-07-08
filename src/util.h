#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdint.h>

int hex_encode_buffer(uint8_t * binary_input_buffer, int num_bytes, char * ascii_output_buffer);

#endif // _UTIL_H_
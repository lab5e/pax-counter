#include "util.h"

int hex_encode_buffer(uint8_t * binary_input_buffer, int num_bytes, char * ascii_output_buffer)
{
	if (binary_input_buffer == nullptr || ascii_output_buffer == nullptr || num_bytes == 0)
		return -1;

	for (int i=0; i<num_bytes; i++) {
		ascii_output_buffer[i*2]   = "0123456789ABCDEF"[binary_input_buffer[i] >> 4];
		ascii_output_buffer[i*2+1] = "0123456789ABCDEF"[binary_input_buffer[i] & 0x0F];
	}
	ascii_output_buffer[num_bytes*2] = '\0';

	return 0;
}
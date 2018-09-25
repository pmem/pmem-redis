#ifndef SHA1_H
#define SHA1_H

#include <stdint.h>
#include <stdlib.h>

struct sha1_context
{
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
};

void sha1_init(struct sha1_context* context);
void sha1_update(struct sha1_context* context, const uint8_t* data, size_t len);
void sha1_final(struct sha1_context* context, uint8_t digest[20]);

#endif

#ifndef GD_EMBEDDED_EFFECTS_H
#define GD_EMBEDDED_EFFECTS_H

#include <stddef.h>

typedef struct {
    const char *name;
    const unsigned char *compressed_data;
    size_t compressed_size;
    size_t uncompressed_size;
} EmbeddedEffect;

const EmbeddedEffect *embedded_effect_find(const char *name);

#endif

#ifndef PHOTO_H
#define PHOTO_H
#include <stdint.h>

typedef enum {
    PHOTO_1 = 0,
    PHOTO_2,
    PHOTO_3,
    PHOTO_4,
    PHOTO_COUNT
} photo_t;

void photo_init(void);
int  photo_read_mV(photo_t sensor, uint32_t *mv);

#endif


#ifndef THERMOMETRY_H
#define THERMOMETRY_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

void thermometry_init(void);

int thermometry_get_temp_and_jpeg(time_t t_head, time_t t_tail, double* temp, uint8_t* jpeg, int size);

#endif /* THERMOMETRY_H */

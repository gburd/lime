#ifndef LIME_LOCATION_H
#define LIME_LOCATION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
** Source location tracking for Lime-generated parsers.
** Used when the %locations directive is active.
*/
typedef struct LimeLocation {
    uint32_t first_line;
    uint32_t first_column;
    uint32_t last_line;
    uint32_t last_column;
    const char *filename;
} LimeLocation;

/* Merge two locations: result spans from start of a to end of b */
static inline LimeLocation lime_location_merge(LimeLocation a, LimeLocation b) {
    LimeLocation result;
    result.first_line = a.first_line;
    result.first_column = a.first_column;
    result.last_line = b.last_line;
    result.last_column = b.last_column;
    result.filename = a.filename;
    return result;
}

/* Create a zero/empty location */
static inline LimeLocation lime_location_none(void) {
    LimeLocation loc = {0, 0, 0, 0, 0};
    return loc;
}

#ifdef __cplusplus
}
#endif

#endif /* LIME_LOCATION_H */

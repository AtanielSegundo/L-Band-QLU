#ifndef QAM16_H
#define QAM16_H

#include <stdint.h>

#ifndef STREAM_DATA_TYPE
#define STREAM_DATA_TYPE
typedef uint16_t stream_data_t;
#endif

#ifndef STREAM_METADATA_TYPE
#define STREAM_METADATA_TYPE
typedef struct {
    uint32_t sampling_rate;
    uint32_t signal_resolution;
    float carrier_freq;
    float samples_per_symbol;
    uint32_t n_samples;
    float scale;
} stream_meta_t;
#endif

static const stream_meta_t qam16_meta = {
    .sampling_rate = 20000000,
    .signal_resolution = 16,
    .carrier_freq = 0.0,
    .samples_per_symbol = 3.0,
    .n_samples = 90,
    .scale = 20752.43333333333,
};


static const stream_data_t qam16[] = {
    33957, 27670, 24062, 51832, 54825, 50839, 22917,
    23306, 24773, 20701, 25970, 29338, 25646, 34212,
    27436, 38368, 37193, 37914, 33526, 29446, 16625,
    38234, 35596, 33562, 27641, 27411, 34447, 40018,
    41680, 34265, 22846, 20732, 10438, 8463, 15028,
    7800, 23003, 24348, 21749, 50053, 44189, 46920,
    18984, 24963, 23163, 14719, 17929, 9917, 13937,
    11761, 13965, 7761, 19612, 22417, 30524, 31050,
    26867, 9967, 17096, 19901, 22526, 28879, 25317,
    22606, 29595, 22801, 32231, 24661, 29136, 1082,
    6609, 563, 27996, 23784, 25273, 28969, 28345,
    27166, 31290, 27461, 24733, 14205, 20726, 11774,
    21389, 21758, 31127, 34684, 38170, 48707
};

#endif /* QAM16_H */

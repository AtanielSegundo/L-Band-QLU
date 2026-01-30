#ifndef QLU_BASE_H

#define QLU_BASE_H


#define RP2040_CORE_0 (1 << 0)
#define RP2040_CORE_1 (1 << 1)

typedef struct {
    double mer;
    double cn0;
    double evm;
} QLUMetrics; 

#endif
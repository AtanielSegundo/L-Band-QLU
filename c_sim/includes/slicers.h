#ifndef SLICERS_H

#define SLICERS_H

#include "base.h"

inline double slicer_calculate_power(double i, double q){
    return (i * i + q * q);
}


typedef struct{
    double ideal_i;
    double ideal_q;
} SlicerResult;


typedef SlicerResult (*slicer_fn_t)(double,double);

SlicerResult bpsk_slicer(double rx_i, double rx_q){
    return (SlicerResult){
        .ideal_i = (rx_i >= 0.0) ? +1.0 : -1.0,
        .ideal_q = (0.0)
    };
};

// Garante que Symbol Power = 1.0
const float QPSK_NORM = 0.7071067812;
SlicerResult qpsk_slicer(double rx_i, double rx_q){
    // QPSK is essentially two BPSK signals in quadrature.
    // We snap the incoming signal to the nearest +/- 1.0 level.
    return (SlicerResult){
        .ideal_i = (rx_i >= 0.0) ? QPSK_NORM : -QPSK_NORM,
        .ideal_q = (rx_q >= 0.0) ? QPSK_NORM : -QPSK_NORM
    };
};

// 16-QAM: Levels +/- 1, +/- 3 must be scaled by 1/sqrt(10)
const float QAM16_NORM = 0.3162277660; 

static inline double slice_pam4(double x) {
    double threshold = 2.0 * QAM16_NORM;
    if (x >= threshold)   return  3.0 * QAM16_NORM;
    if (x >= 0.0)         return  1.0 * QAM16_NORM;
    if (x >= -threshold)  return -1.0 * QAM16_NORM;
    return -3.0 * QAM16_NORM;
}

SlicerResult qam16_slicer(double rx_i, double rx_q){
    return (SlicerResult){
        .ideal_i = slice_pam4(rx_i),
        .ideal_q = slice_pam4(rx_q)
    };
};

slicer_fn_t get_slicer_by_mod[] = {
    [MOD_BPSK]  = bpsk_slicer,
    [MOD_QPSK]  = qpsk_slicer,
    [MOD_16QAM] = qam16_slicer
};

#endif
"""
skew.py — IQ Imbalance (Polarization Skew) Simulation & Measurement

[Context]
In Ku-band VSAT, "skew" refers to the rotational misalignment of the LNB/feed
relative to the satellite's polarization plane. A misaligned LNB introduces:

  1. Phase imbalance  — I and Q channels are no longer perfectly 90° apart
  2. Amplitude imbalance — I and Q channels have different gain

Both effects are modeled in the complex baseband as IQ imbalance.

Physical reference (SkyEdge II-c Gemini-4 return channel):
  - Acceptable range: phase < 5°, amplitude < 0.5 dB
  - Degraded:         phase 5–15°, amplitude 0.5–2 dB
  - Critical:         phase > 15°, amplitude > 2 dB
"""

import numpy as np

from modulations.bpsk  import BpskModem
from modulations.qpsk  import QpskModem
from modulations.qam16 import Qam16Modem


def apply_iq_skew(
    complex_iq: np.ndarray,
    phase_imbalance_deg: float = 0.0,
    amplitude_imbalance_db: float = 0.0,
) -> np.ndarray:
    """
    Applies IQ imbalance (skew effect) to a complex baseband signal.

    The model used is the standard linear IQ mismatch:

        I_out = (1 + alpha) * [I * cos(phi/2) + Q * sin(phi/2)]
        Q_out =               [-I * sin(phi/2) + Q * cos(phi/2)]

    Where:
        phi   = phase imbalance in radians
        alpha = amplitude imbalance as linear delta (not ratio)

    Parameters:
        complex_iq            : complex64 array, complex baseband samples
        phase_imbalance_deg   : phase error between I and Q (degrees)
                                0° = ideal, >15° = severe degradation
        amplitude_imbalance_db: gain difference between I and Q channels (dB)
                                0 dB = ideal, >3 dB = severe degradation

    Returns:
        np.ndarray: skewed complex baseband signal (complex64)
    """
    phi   = np.radians(phase_imbalance_deg)
    alpha = 10.0 ** (amplitude_imbalance_db / 20.0) - 1.0

    I = complex_iq.real.astype(np.float64)
    Q = complex_iq.imag.astype(np.float64)

    I_out = (1.0 + alpha) * (I * np.cos(phi / 2.0) + Q * np.sin(phi / 2.0))
    Q_out =                  (-I * np.sin(phi / 2.0) + Q * np.cos(phi / 2.0))

    return (I_out + 1j * Q_out).astype(np.complex64)


def measure_iq_imbalance(complex_iq: np.ndarray, modem_cls=None):
    """
    Estimates IQ imbalance from received complex baseband samples using
    error-vector decomposition after decision-directed slicing.

    The raw power-ratio method (mean(I²)/mean(Q²)) fails for BPSK because
    BPSK places all signal energy on I with Q≈0, reporting false amplitude
    imbalance even with perfect alignment.

    This method slices to the nearest ideal constellation point, then measures
    the I/Q asymmetry of the ERROR vectors (received − ideal). Since AWGN is
    isotropic, error_I² ≈ error_Q² when there is no skew, regardless of
    modulation. IQ skew breaks that symmetry.

    Parameters:
        complex_iq : complex baseband samples
        modem_cls  : BpskModem / QpskModem / Qam16Modem class (for slicer).
                     If None, falls back to raw power-ratio (legacy behaviour).

    Returns:
        amplitude_imbalance_db  (float): positive = error_I stronger
        phase_imbalance_deg     (float): signed phase error in degrees
    """
    if modem_cls is None:
        # Legacy fallback: raw power-ratio (works for QPSK/16QAM, bad for BPSK)
        I = complex_iq.real.astype(np.float64)
        Q = complex_iq.imag.astype(np.float64)
        power_I = float(np.mean(I ** 2))
        power_Q = float(np.mean(Q ** 2))
        cross   = float(np.mean(I * Q))
    else:
        # Error-vector method: slice then measure error asymmetry
        avg_pwr = float(np.mean(np.abs(complex_iq) ** 2))
        if avg_pwr < 1e-12:
            return 0.0, 0.0
        y = complex_iq / np.sqrt(avg_pwr)

        if modem_cls is BpskModem:
            ideal = np.sign(y.real) + 0j
        elif modem_cls is QpskModem:
            norm = 1.0 / np.sqrt(2.0)
            ideal = (np.sign(y.real) + 1j * np.sign(y.imag)) * norm
        elif modem_cls is Qam16Modem:
            norm = 1.0 / np.sqrt(10.0)
            levels = np.array([-3.0, -1.0, 1.0, 3.0])
            def _sl(v):
                dist = np.abs(v[:, None] / norm - levels)
                return levels[np.argmin(dist, axis=1)] * norm
            ideal = _sl(y.real) + 1j * _sl(y.imag)
        else:
            ideal = y / (np.abs(y) + 1e-12)

        err = y - ideal
        eI = err.real.astype(np.float64)
        eQ = err.imag.astype(np.float64)
        power_I = float(np.mean(eI ** 2))
        power_Q = float(np.mean(eQ ** 2))
        cross   = float(np.mean(eI * eQ))

    amplitude_imbalance_db = 10.0 * np.log10((power_I + 1e-12) / (power_Q + 1e-12))

    denom = np.sqrt(power_I * power_Q) + 1e-12
    arg = np.clip(2.0 * cross / denom, -1.0, 1.0)
    phase_imbalance_deg = float(np.degrees(np.arcsin(arg)))

    return amplitude_imbalance_db, phase_imbalance_deg


def skew_score(amplitude_imbalance_db: float, phase_imbalance_deg: float) -> float:
    """
    Converts IQ imbalance parameters into a skew quality score (0–100).

    Thresholds are calibrated for Ku-band VSAT field installation:
        Phase:     0°  → 100 pts,  ≥15° → 0 pts  (linear decay)
        Amplitude: 0dB → 100 pts,  ≥3dB → 0 pts  (linear decay)

    Returns:
        float: 0.0 (worst) to 100.0 (perfect alignment)
    """
    PHASE_MAX_DEG = 15.0   # degradation ceiling for phase
    AMP_MAX_DB    = 3.0    # degradation ceiling for amplitude

    phase_score = max(0.0, 100.0 - abs(phase_imbalance_deg) * (100.0 / PHASE_MAX_DEG))
    amp_score   = max(0.0, 100.0 - abs(amplitude_imbalance_db) * (100.0 / AMP_MAX_DB))

    return (phase_score + amp_score) / 2.0


def skew_range_sweep(
    complex_iq: np.ndarray,
    phase_range=(0, 20, 5),
    amplitude_range=(0, 4, 1),
):
    """
    Generates a matrix of (phase, amplitude) → skew_score for plotting.

    Useful for visualizing how degradation evolves with misalignment.

    Parameters:
        complex_iq       : reference clean or noisy signal
        phase_range      : (start, stop, step) in degrees
        amplitude_range  : (start, stop, step) in dB

    Returns:
        results (list of dicts): each entry has phase_deg, amplitude_db,
                                 measured_phase, measured_amp, score
    """
    results = []
    phases = np.arange(*phase_range)
    amps   = np.arange(*amplitude_range)

    for phi in phases:
        for amp in amps:
            skewed = apply_iq_skew(complex_iq, phi, amp)
            meas_amp, meas_phase = measure_iq_imbalance(skewed)
            score = skew_score(meas_amp, meas_phase)
            results.append({
                "applied_phase_deg":   phi,
                "applied_amp_db":      amp,
                "measured_phase_deg":  meas_phase,
                "measured_amp_db":     meas_amp,
                "skew_score":          score,
            })

    return results
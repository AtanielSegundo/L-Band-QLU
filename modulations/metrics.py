"""
metrics.py — MER, Signal Stability, and SQI (Signal Quality Index)

[Context]
Target modem: SkyEdge II-c Gemini-4 (Ku-band VSAT return channel)
  Supported return modulations: BPSK, QPSK, 8PSK, 16QAM (TDMA, TPC coded)
  Return symbol rates: 128 Ksps – 4 Msps, 6 Msps

SQI is a unified 0–100 installation quality score combining:
    ┌─────────────────────────────────────────────────┐
    │  SQI = 0.40·MER_n + 0.30·CN0_n                 │
    │       + 0.20·Skew_n + 0.10·Stability_n          │
    └─────────────────────────────────────────────────┘

Grade bands:
    90–100  Excellent  — optimal installation, no action needed
    75–89   Good       — acceptable, minor adjustments may improve
    55–74   Fair       — degraded, realignment recommended
    30–54   Poor       — link at risk, immediate action required
     0–29   Critical   — link likely unusable
"""

import numpy as np
from dataclasses import dataclass
from typing import Type

# ---------------------------------------------------------------------------
# Local imports (adjust path if using as standalone module)
# ---------------------------------------------------------------------------
try:
    from modulations.bpsk  import BpskModem
    from modulations.qpsk  import QpskModem
    from modulations.qam16 import Qam16Modem
except ImportError:
    BpskModem = QpskModem = Qam16Modem = None


# ---------------------------------------------------------------------------
# Data class for the full metric report
# ---------------------------------------------------------------------------

@dataclass
class SignalReport:
    """Complete signal quality report for one measurement snapshot."""

    # --- Raw metrics ---
    mer_db:               float   # Modulation Error Ratio (dB)
    cn0_dbhz:             float   # Carrier-to-Noise density (dB-Hz)
    amplitude_imbalance_db: float # IQ amplitude imbalance (dB)
    phase_imbalance_deg:  float   # IQ phase imbalance (degrees)
    stability_pct:        float   # Signal power stability (0–100 %)

    # --- Normalised sub-scores (0–100) ---
    mer_score:       float
    cn0_score:       float
    skew_score:      float
    stability_score: float

    # --- Unified index ---
    sqi:  float   # Signal Quality Index, 0–100

    @property
    def grade(self) -> str:
        if self.sqi >= 90: return "Excellent"
        if self.sqi >= 75: return "Good"
        if self.sqi >= 55: return "Fair"
        if self.sqi >= 30: return "Poor"
        return "Critical"

    def summary(self) -> str:
        return (
            f"SQI={self.sqi:.1f} [{self.grade}] | "
            f"MER={self.mer_db:.1f}dB  C/N0={self.cn0_dbhz:.1f}dBHz  "
            f"Skew(φ={self.phase_imbalance_deg:.1f}°, A={self.amplitude_imbalance_db:.2f}dB)  "
            f"Stability={self.stability_pct:.1f}%"
        )


# ---------------------------------------------------------------------------
# MER — Modulation Error Ratio
# ---------------------------------------------------------------------------

def calculate_mer(
    complex_iq:    np.ndarray,
    modem_cls:     Type,
    sampling_rate: float,
) -> float:
    """
    Computes MER (Modulation Error Ratio) in dB using a decision-directed slicer.

    MER (dB) = 10 · log10( P_signal / P_error )

    Where P_error = mean squared distance from the nearest ideal constellation point.
    This is equivalent to EVM² inverted.

    Parameters:
        complex_iq    : complex baseband samples
        modem_cls     : BpskModem, QpskModem, or Qam16Modem class (not instance)
        sampling_rate : sampling rate in Hz (used for reference only in this fn)

    Returns:
        float: MER in dB. Higher is better.
               BPSK good: >12 dB | QPSK good: >15 dB | 16QAM good: >22 dB
    """
    if len(complex_iq) == 0:
        return 0.0

    # Normalise to unit average power before slicing
    avg_pwr = float(np.mean(np.abs(complex_iq) ** 2))
    if avg_pwr < 1e-12:
        return 0.0
    y = complex_iq / np.sqrt(avg_pwr)

    if modem_cls is BpskModem:
        # Ideal points: ±1 on I axis
        ideal = np.sign(y.real) + 0j

    elif modem_cls is QpskModem:
        # Ideal points: (±1/√2, ±1/√2)
        norm = 1.0 / np.sqrt(2.0)
        ideal = (np.sign(y.real) + 1j * np.sign(y.imag)) * norm

    elif modem_cls is Qam16Modem:
        # Ideal points: {±1, ±3}/√10 per axis
        norm = 1.0 / np.sqrt(10.0)
        levels = np.array([-3.0, -1.0, 1.0, 3.0])

        def slice_pam4(v):
            scaled = v / norm                          # scale to {±1,±3} space
            dist   = np.abs(scaled[:, None] - levels)  # (N, 4) distances
            return levels[np.argmin(dist, axis=1)]

        I_ideal = slice_pam4(y.real)
        Q_ideal = slice_pam4(y.imag)
        ideal   = (I_ideal + 1j * Q_ideal) * norm

    else:
        # Fallback: unit-circle slicer
        ideal = y / (np.abs(y) + 1e-12)

    error_vec    = y - ideal
    p_signal     = float(np.mean(np.abs(ideal) ** 2))
    p_error      = float(np.mean(np.abs(error_vec) ** 2))

    if p_error < 1e-12:
        return 60.0  # virtually noise-free

    return float(10.0 * np.log10(p_signal / p_error))


# ---------------------------------------------------------------------------
# Signal Stability
# ---------------------------------------------------------------------------

def signal_stability(
    complex_iq:        np.ndarray,
    sampling_rate:     float,
    window_size_ms:    float = 100.0,
) -> float:
    """
    Measures temporal power stability of the received signal.

    Method: divide signal into time windows, compute power per window,
    then calculate the coefficient of variation (CV = σ/μ).
    A perfectly stable signal has CV=0 → 100% score.

    Parameters:
        complex_iq     : complex baseband samples
        sampling_rate  : Hz
        window_size_ms : window length in milliseconds

    Returns:
        float: stability score 0–100. 100 = perfectly stable.
    """
    samples_per_window = max(1, int(sampling_rate * window_size_ms / 1000.0))
    n_windows = len(complex_iq) // samples_per_window

    if n_windows < 2:
        return 100.0  # signal too short to judge — assume stable

    powers = np.array([
        float(np.mean(np.abs(complex_iq[i * samples_per_window:
                                        (i + 1) * samples_per_window]) ** 2))
        for i in range(n_windows)
    ])

    mean_pwr = float(np.mean(powers)) + 1e-12
    cv = float(np.std(powers)) / mean_pwr

    # CV = 0 → 100pts, CV ≥ 0.30 → 0pts  (linear)
    CV_CEILING = 0.30
    return float(max(0.0, 100.0 * (1.0 - cv / CV_CEILING)))


# ---------------------------------------------------------------------------
# Normalisation helpers (Ku-band VSAT reference ranges)
# ---------------------------------------------------------------------------

def _normalise_mer(mer_db: float, modem_cls: Type) -> float:
    """
    Normalises MER to 0–100 using per-modulation thresholds.

    Threshold table (Ku-band Gemini-4 TPC-coded return link):
        BPSK  : floor =  5 dB,  ceiling = 20 dB
        QPSK  : floor =  8 dB,  ceiling = 23 dB
        16QAM : floor = 15 dB,  ceiling = 32 dB
    """
    thresholds = {
        "BpskModem":  ( 5.0, 20.0),
        "QpskModem":  ( 8.0, 23.0),
        "Qam16Modem": (15.0, 32.0),
    }
    name = modem_cls.__name__ if modem_cls is not None else "QpskModem"
    floor, ceiling = thresholds.get(name, (8.0, 25.0))
    return float(np.clip((mer_db - floor) / (ceiling - floor) * 100.0, 0.0, 100.0))


def _normalise_cn0(cn0_dbhz: float) -> float:
    """
    Normalises C/N₀ to 0–100 for Ku-band VSAT context.

    Reference range:
        ≤ 50 dB-Hz → 0   (below any usable threshold)
        ≥ 80 dB-Hz → 100 (excellent link margin)
    """
    CN0_FLOOR   = 50.0
    CN0_CEILING = 80.0
    return float(np.clip((cn0_dbhz - CN0_FLOOR) / (CN0_CEILING - CN0_FLOOR) * 100.0, 0.0, 100.0))


# ---------------------------------------------------------------------------
# C/N₀ estimator (re-exported here for convenience)
# ---------------------------------------------------------------------------

def calculate_cn0(
    complex_iq:    np.ndarray,
    sampling_rate: float,
    modem_cls:     Type,
) -> float:
    """
    Estimates C/N₀ using a decision-directed EVM method.

    C/N₀ (dB-Hz) = SNR (dB) + 10·log10(sampling_rate)

    This is the same implementation from generate_stream.py, consolidated here
    so metrics.py is a single import for the measurement pipeline.
    """
    # MER is numerically identical to SNR in the decision-directed model
    snr_db = calculate_mer(complex_iq, modem_cls, sampling_rate)
    return float(snr_db + 10.0 * np.log10(sampling_rate))


# ---------------------------------------------------------------------------
# Unified SQI — Signal Quality Index
# ---------------------------------------------------------------------------

def calculate_sqi(
    complex_iq:             np.ndarray,
    modem_cls:              Type,
    sampling_rate:          float,
    amplitude_imbalance_db: float = 0.0,
    phase_imbalance_deg:    float = 0.0,
    window_size_ms:         float = 100.0,
) -> SignalReport:
    """
    Computes the unified Signal Quality Index (SQI) and full SignalReport.

    SQI = 0.40 · MER_score
        + 0.30 · C/N₀_score
        + 0.20 · Skew_score
        + 0.10 · Stability_score

    Weight rationale (Ku-band VSAT installation context):
        MER       40% — direct measure of demodulation quality; most sensitive
                         to the combined effect of noise + distortion
        C/N₀      30% — link budget metric; reflects dish pointing accuracy
        Skew      20% — polarisation alignment; critical for cross-pol isolation
        Stability 10% — power envelope steadiness; flags obstructions/multipath

    Parameters:
        complex_iq             : complex baseband samples (with skew applied if any)
        modem_cls              : modem class (BpskModem / QpskModem / Qam16Modem)
        sampling_rate          : Hz
        amplitude_imbalance_db : IQ amplitude imbalance (from skew.measure_iq_imbalance)
        phase_imbalance_deg    : IQ phase imbalance in degrees
        window_size_ms         : window size for stability calculation

    Returns:
        SignalReport dataclass with all raw and normalised metrics + SQI
    """
    from modulations.metrics import (
        calculate_mer, calculate_cn0, signal_stability,
        _normalise_mer, _normalise_cn0,
    )
    from modulations.skew import skew_score as _skew_score

    # --- Compute raw metrics ---
    mer_db      = calculate_mer(complex_iq, modem_cls, sampling_rate)
    cn0_dbhz    = calculate_cn0(complex_iq, sampling_rate, modem_cls)
    stability   = signal_stability(complex_iq, sampling_rate, window_size_ms)

    # --- Normalise to 0–100 ---
    mer_n       = _normalise_mer(mer_db, modem_cls)
    cn0_n       = _normalise_cn0(cn0_dbhz)
    skew_n      = _skew_score(amplitude_imbalance_db, phase_imbalance_deg)
    stability_n = stability  # already 0–100

    # --- Weighted sum ---
    W_MER       = 0.40
    W_CN0       = 0.30
    W_SKEW      = 0.20
    W_STABILITY = 0.10

    sqi = (
        W_MER       * mer_n
        + W_CN0       * cn0_n
        + W_SKEW      * skew_n
        + W_STABILITY * stability_n
    )

    return SignalReport(
        mer_db               = mer_db,
        cn0_dbhz             = cn0_dbhz,
        amplitude_imbalance_db = amplitude_imbalance_db,
        phase_imbalance_deg  = phase_imbalance_deg,
        stability_pct        = stability,
        mer_score            = mer_n,
        cn0_score            = cn0_n,
        skew_score           = skew_n,
        stability_score      = stability_n,
        sqi                  = float(np.clip(sqi, 0.0, 100.0)),
    )
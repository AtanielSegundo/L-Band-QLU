"""
generate_stream_with_sqi.py
Integration example: adds skew simulation and SQI to the existing pipeline.

Changes vs original generate_stream.py:
  1. Import skew.py and metrics.py
  2. apply_iq_skew() after modulate()
  3. measure_iq_imbalance() on the received signal  
  4. calculate_sqi() to get the unified report
  5. SDR simulator sweep now also includes skew levels
"""

import warnings
warnings.filterwarnings("ignore", category=UserWarning)

import os
import shutil
from pathlib import Path

import numpy as np

from modulations.bpsk  import BpskModem
from modulations.qpsk  import QpskModem
from modulations.qam16 import Qam16Modem
from modulations.util  import MegaHz, bytes_to_bits, bits_to_bytes, tx_rx_error

from modulations.skew    import apply_iq_skew, measure_iq_imbalance, skew_range_sweep
from modulations.metrics import calculate_mer, calculate_cn0, signal_stability, calculate_sqi
from convert import ToHeaderConverter

SKEW_SCENARIOS = [
    # label                 phase_deg  amplitude_db   description
    ("perfect",             0.0,       0.0,           "LNB perfectly aligned"),
    ("moderate_phase",      8.0,       0.0,           "Moderate misalignment (~1 turn of LNB)"),
    ("combined_moderate",   6.0,       0.8,           "Realistic degraded installation"),
    ("combined_severe",    12.0,       2.0,           "Poorly installed site"),
]


def main():
    stream_str   = "t2OcohNI8LVpbG28G4mV7R8Ht34YJSyKQMbrIwerCKnJvTXKdybsJCKclGk3xNoBwlR58RslAN4pAwjJq4fTpL7aIH44wlOK63468oUbjrZhE5rOq4uAwmB40w98tRDqS35WbZdLM7bNbzCHf7r2YlT70U7KY1jv8BsQSrZwqIx873tL2"
    byte_stream  = bytearray(stream_str, encoding="ascii")
    bit_stream   = bytes_to_bits(byte_stream, msb_first=True)

    link_bw      = MegaHz(10.0)
    sampling_rate = 2 * link_bw
    roll_off     = 0.25
    SNR_db       = 15
    SEED         = 333
    AMPLITUDE    = 1.0
    Fc           = 0.0

    modems_cls   = [BpskModem, QpskModem, Qam16Modem]

    print("=" * 70)
    print("  SQI DEMONSTRATION — Skew Scenarios x Modulations")
    print("  Target: SkyEdge II-c Gemini-4 (Ku-band return channel)")
    print("=" * 70)

    for modem_cls in modems_cls:
        modem      = modem_cls(sampling_rate, link_bw, roll_off)
        modem_name = type(modem).__name__

        # --- Modulate (no skew yet — get clean complex baseband) ---
        _, complex_iq_clean, info = modem.modulate(
            bit_stream, fc=Fc, snr_db=SNR_db, seed=SEED, amplitude=AMPLITUDE
        )

        print(f"\n{'─'*70}")
        print(f"  Modulation: {modem_name}  |  Bit-rate: {info.bit_rate/1e6:.2f} Mbps  |  SNR: {SNR_db} dB")
        print(f"{'─'*70}")
        print(f"  {'Scenario':<22} {'SQI':>6} {'Grade':<12} {'MER':>8} {'C/N0':>10} {'Skew':>8} {'Stab':>8}")
        print(f"  {'─'*22} {'─'*6} {'─'*12} {'─'*8} {'─'*10} {'─'*8} {'─'*8}")

        for label, phase_deg, amp_db, _ in SKEW_SCENARIOS:
            # 1. Apply skew to the clean complex baseband
            skewed_iq = apply_iq_skew(complex_iq_clean, phase_deg, amp_db)

            # 2. Measure IQ imbalance from the received signal
            meas_amp, meas_phase = measure_iq_imbalance(skewed_iq, modem_cls=modem_cls)

            # 3. Calculate SQI
            report = calculate_sqi(
                complex_iq             = skewed_iq,
                modem_cls              = modem_cls,
                sampling_rate          = sampling_rate,
                amplitude_imbalance_db = meas_amp,
                phase_imbalance_deg    = meas_phase,
            )

            print(
                f"  {label:<22} {report.sqi:>6.1f} {report.grade:<12} "
                f"{report.mer_db:>7.1f}dB "
                f"{report.cn0_dbhz:>9.1f}dBHz "
                f"{report.skew_score:>7.1f}pt "
                f"{report.stability_pct:>7.1f}%"
            )

    # ===========================================================================
    # SDR SIMULATOR — generate headers WITH skew variants
    # Each header now carries: modulation × SNR × skew_scenario
    # ===========================================================================
    print("\n\n[INFO] Generating SDR Simulator headers with skew variants...")

    snr_range    = list(range(1, 41, 2))
    h_converter  = ToHeaderConverter(resolution=16)
    folder_path  = "sdr_simulator/headers"

    if os.path.exists(folder_path):
        shutil.rmtree(folder_path)
    os.makedirs(folder_path, exist_ok=True)

    for snr in snr_range:
        for modem_cls in modems_cls:
            modem      = modem_cls(sampling_rate, link_bw, roll_off)
            modem_name = modem_cls.__name__
            mod_label  = modem_name.split("Modem")[0].lower()

            _, complex_iq_clean, info = modem.modulate(
                bit_stream, fc=Fc, snr_db=snr, seed=SEED, amplitude=AMPLITUDE
            )

            for sk_label, phase_deg, amp_db, _ in SKEW_SCENARIOS:
                skewed_iq         = apply_iq_skew(complex_iq_clean, phase_deg, amp_db)
                meas_amp, meas_phase = measure_iq_imbalance(skewed_iq, modem_cls=modem_cls)

                arr_name    = f"complex_{mod_label}_{sk_label}_{snr}"
                header_name = arr_name + ".h"
                save_path   = os.path.join(folder_path, header_name)

                # Embed SQI in header comment via arr metadata
                h_converter.complex_st(
                    skewed_iq, info,
                    save_path  = save_path,
                    arr_name   = arr_name,
                    elem_per_line = 7,
                )

    # ===========================================================================
    # SIMULATION BASE HEADER — lookup tables for modulation × skew × SNR
    # ===========================================================================
    print(f"[INFO] Generating Simulation Base Header")

    available_modulations = [cls.__name__.split("Modem")[0].lower() for cls in modems_cls]
    skew_labels           = [label for label, _, _, _ in SKEW_SCENARIOS]

    PAD   = " " * 4
    lines = []

    lines.append("#ifndef SIMULATION_BASE_H")
    lines.append("\n")
    lines.append("#define SIMULATION_BASE_H")
    lines.append("\n")

    # includes
    for snr in snr_range:
        for mod in available_modulations:
            for sk_label in skew_labels:
                lines.append(f'#include "complex_{mod}_{sk_label}_{snr}.h"')
    lines.append("\n")

    # Modulations enum
    lines.append("typedef enum {")
    for mod in available_modulations:
        lines.append(f"{PAD}{mod.upper()},")
    lines.append(f"{PAD}MODULATIONS_COUNT")
    lines.append("} Modulations;\n")

    # Modulation name strings
    lines.append("const char* modulations_str[] = {")
    for mod in available_modulations:
        lines.append(f'{PAD}"{mod.upper()}",')
    lines.append("};\n")

    # SkewScenarios enum
    lines.append("typedef enum {")
    for sk_label in skew_labels:
        lines.append(f"{PAD}SKEW_{sk_label.upper()},")
    lines.append(f"{PAD}SKEW_COUNT")
    lines.append("} SkewScenarios;\n")

    # Skew name strings
    lines.append("const char* skew_str[] = {")
    for sk_label in skew_labels:
        lines.append(f'{PAD}"{sk_label}",')
    lines.append("};\n")

    # SNR array
    lines.append(f"unsigned int available_snr[] = {{{','.join(map(str, snr_range))}}};")
    lines.append(f"size_t available_snr_count = {len(snr_range)};\n")

    # Per modulation × skew: data and meta lookup by SNR index
    for mod in available_modulations:
        for sk_label in skew_labels:
            # data array
            lines.append(f"const stream_data_t* {mod}_{sk_label}_by_snr[] = " + "{")
            for snr in range(0, snr_range[-1] + 1):
                name = f"complex_{mod}_{sk_label}_{snr}"
                if snr not in snr_range:
                    lines.append(f"{PAD}nullptr,")
                else:
                    lines.append(f"{PAD}{name},")
            lines.append("};\n")

            # meta array
            lines.append(f"const stream_meta_t* {mod}_{sk_label}_meta_by_snr[] = " + "{")
            for snr in range(0, snr_range[-1] + 1):
                name = f"&complex_{mod}_{sk_label}_{snr}_meta"
                if snr not in snr_range:
                    lines.append(f"{PAD}nullptr,")
                else:
                    lines.append(f"{PAD}{name},")
            lines.append("};\n")

    # Per modulation: array of skew scenario pointers (data)
    for mod in available_modulations:
        lines.append(f"const stream_data_t** {mod}_data_by_skew[] = " + "{")
        for sk_label in skew_labels:
            lines.append(f"{PAD}[SKEW_{sk_label.upper()}] = {mod}_{sk_label}_by_snr,")
        lines.append("};\n")

        lines.append(f"const stream_meta_t** {mod}_meta_by_skew[] = " + "{")
        for sk_label in skew_labels:
            lines.append(f"{PAD}[SKEW_{sk_label.upper()}] = {mod}_{sk_label}_meta_by_snr,")
        lines.append("};\n")

    # Top-level: data_by_modulation[mod][skew][snr]
    lines.append("const stream_data_t*** data_by_modulation[] = " + "{")
    for mod in available_modulations:
        lines.append(f"{PAD}[{mod.upper()}] = {mod}_data_by_skew,")
    lines.append("};\n")

    lines.append("const stream_meta_t*** meta_by_modulation[] = " + "{")
    for mod in available_modulations:
        lines.append(f"{PAD}[{mod.upper()}] = {mod}_meta_by_skew,")
    lines.append("};\n")

    # Accessor macros
    lines.append("#define GET_DATA(modulation,skew,snr) (data_by_modulation[modulation][skew][snr])")
    lines.append("#define GET_META(modulation,skew,snr) (meta_by_modulation[modulation][skew][snr])")
    lines.append("#define GET_MODULATION_NAME(modulation) (modulations_str[modulation])")
    lines.append("#define GET_SKEW_NAME(skew) (skew_str[skew])")

    lines.append("\n#endif")

    header_text = "\n".join(lines)
    Path(os.path.join(folder_path, "simulation_base.h")).write_text(header_text)

    print(f"[INFO] Done — headers in {folder_path}/")
    print(f"[INFO] Total headers: {len(snr_range)} SNRs × "
          f"{len(modems_cls)} modulations × {len(SKEW_SCENARIOS)} skew scenarios = "
          f"{len(snr_range) * len(modems_cls) * len(SKEW_SCENARIOS)} files")


if __name__ == "__main__":
    main()
import warnings
warnings.filterwarnings("ignore", category=UserWarning)

import os 
import shutil
from pathlib import Path

from modulations.bpsk import BpskModem
from modulations.qpsk import QpskModem
from modulations.qam16 import Qam16Modem 
from modulations.util import MegaHz,bytes_to_bits,bits_to_bytes,tx_rx_error

from convert import ToHeaderConverter


import numpy as np

def calculate_cn0(complex_iq, sampling_rate, modem):
    """
    Estimates C/N0 (Carrier-to-Noise density ratio) from complex I/Q samples.
    
    Method: Decision-Directed (EVM based).
    Formula: C/N0_dBHz = SNR_dB + 10*log10(Sampling_Rate)
    
    Parameters:
        complex_iq (np.array): Complex signal samples (signal + noise).
        sampling_rate (float): Sampling rate in Hz.
        mod_type (str): 'bpsk', 'qpsk', or '16qam'.
        
    Returns:
        float: Estimated C/N0 in dB-Hz.
    """
    avg_power = np.mean(np.abs(complex_iq)**2)
    if avg_power == 0: return 0.0
    y = complex_iq / np.sqrt(avg_power)
    
    
    if modem is BpskModem:
        ideal = np.sign(y.real) + 0j
        
    elif modem is QpskModem:
        norm_factor = 1.0/np.sqrt(2)
        ideal = (np.sign(y.real) + 1j*np.sign(y.imag)) * norm_factor
        
    elif modem is Qam16Modem:
        norm_factor = 1.0/np.sqrt(10)
        
        def slice_pam4(v):
            scaled = v / norm_factor
            clipped = np.clip(scaled, -3, 3)
            return 2.0 * np.round((clipped - 1.0) / 2.0) + 1.0

        ideal_r = slice_pam4(y.real)
        ideal_i = slice_pam4(y.imag)
        ideal = (ideal_r + 1j*ideal_i) * norm_factor
        
    else:
        ideal = y / (np.abs(y) + 1e-12)

    noise_vec = y - ideal
    noise_power = np.mean(np.abs(noise_vec)**2)
    
    signal_power = np.mean(np.abs(ideal)**2)
    
    if noise_power == 0: return float('inf')
    
    snr_linear = signal_power / noise_power
    snr_db = 10.0 * np.log10(snr_linear)
    
    cn0_dbhz = snr_db + 10.0 * np.log10(sampling_rate)
    
    return cn0_dbhz

def main():
    stream_str = "Hello IQ Stream"
    byte_stream = bytearray(stream_str,encoding="ascii")

    link_bw  = MegaHz(83.75)
    link_bw  = MegaHz(10.0)

    sampling_rate = 2 * link_bw
    roll_off = 0.25

    print("[INFO] Starting I/Q Stream Generation For C Simulation")

    bit_stream = bytes_to_bits(byte_stream, msb_first=True)
    
    modems_cls = [BpskModem,QpskModem,Qam16Modem]
    
    Fc = 0.0
    # Fc     = MegaHz(1105) 
    
    SNR_db    = 15
    
    SEED      = 333
    AMPLITUDE = 1.0
    
    # resolution dictates the bits by float data
    # in iq_t is the real signal, that should be in the resolution range
    # in complex_iq is the same but for each I Q pair, so I[0] is 16 bits and Q[0] is 16 bits too
    h_converter = ToHeaderConverter(resolution=16)

    print("-----------------------------------------------------------------------------")
    for modem_cls in modems_cls:
        modem = modem_cls(sampling_rate,link_bw,roll_off)
        modem_name = type(modem).__name__

        iq_t, complex_iq, info = modem.modulate(bit_stream,fc=Fc,snr_db=SNR_db,seed=SEED,
                                                amplitude=AMPLITUDE)
        _, zer_complex_iq, _ = modem.modulate(bit_stream,fc=0.0,snr_db=SNR_db,seed=SEED,
                                              amplitude=AMPLITUDE)
        zer_eq = sum(complex_iq == zer_complex_iq) / (min(len(complex_iq),len(zer_complex_iq)))
        print("ZERO == CARRIER IQ? ",bool(zer_eq))
        
        cn0_est = calculate_cn0(complex_iq, sampling_rate, modem)

        modem.plot(f"plots/test_{modem_name}.png",iq_t,complex_iq,info)
        dem_bit_stream = modem.demodulate(iq_t,info)
        dem_bytes = bits_to_bytes(dem_bit_stream,msb_first=True)

        h_name = modem_name.split("Modem")[0].lower()
        
        # h_converter.real_st(iq_t,info,save_path=f"headers/{h_name}.h",
        #                     arr_name=h_name,
        #                     elem_per_line=7)
        
        h_converter.complex_st(complex_iq,info,save_path=f"headers/complex_{h_name}.h",
                               arr_name="complex_"+h_name,
                               elem_per_line=7)

        print(f"{modem_name} Bit rate: {info.bit_rate}")
        print("OK:", dem_bytes == byte_stream)
        print(f"  C/N0:     {cn0_est:.2f} dB-Hz (Est)")
        print(f"Error: {round(tx_rx_error(byte_stream,dem_bytes) * 100,2)}%")
        print("-----------------------------------------------------------------------------")


    print("[INFO] Starting I/Q Stream Generation For SDR SIMULATOR")

    snr_generation_range = list(range(5,40+1,1))
    folder_path = "sdr_simulator/headers"
    if os.path.exists(folder_path): shutil.rmtree(folder_path)
    os.makedirs(folder_path,exist_ok=True)
    created_headers = []
    for snr in snr_generation_range:
        for modem_cls in modems_cls:
            modem = modem_cls(sampling_rate,link_bw,roll_off)
            modem_name = type(modem).__name__

            iq_t, complex_iq, info = modem.modulate(bit_stream,fc=Fc,snr_db=snr,seed=SEED, amplitude=AMPLITUDE)
            cn0_est = calculate_cn0(complex_iq, sampling_rate, modem)

            modulation_name = modem_name.split("Modem")[0].lower()
            arr_name =  f"complex_{modulation_name}_{snr}"
            header_name = arr_name + ".h"
            created_headers.append(header_name)
            save_path = os.path.join(folder_path,header_name)
            
            h_converter.complex_st(complex_iq,info,save_path=save_path,arr_name=arr_name,elem_per_line=7)

    print(f"[INFO] Generating Simulation Base Header")

    available_modulations = list(map(lambda s: s.__name__.split("Modem")[0].lower(),modems_cls)) 
    PAD = " " * 4
    lines = []

    lines.append("#ifndef SIMULATION_BASE_H")
    lines.append("\n")
    lines.append("#define SIMULATION_BASE_H")
    lines.append("\n")
    
    for c_header in created_headers:
        lines.append(f'#include "{c_header}"')
    lines.append("\n")
    
    lines.append("typedef enum {")
    for modulation in available_modulations:
        lines.append(f"{PAD}{modulation.upper()},")
    lines.append(f"{PAD}MODULATIONS_COUNT")
    lines.append("} Modulations;\n")
    
    lines.append("const char* modulations_str[] = {")
    for modulation in available_modulations:
        lines.append(f'{PAD}"{modulation.upper()}",')
    lines.append("};\n")

    lines.append(f"unsigned int available_snr[] = {'{'}{','.join(map(str,snr_generation_range))}{'}'};")
    lines.append(f"size_t available_snr_count = {len(snr_generation_range)};\n")

    for modulation in available_modulations:
        lines.append(f"const stream_data_t* {modulation}_by_snr[] = "+"{")
        for snr in range(0,snr_generation_range[-1]+1):
            name = f"complex_{modulation}_{snr}"
            if snr not in snr_generation_range:
                lines.append(f"{PAD}nullptr,")
            else:
                lines.append(f"{PAD}{name},")
        lines.append("};\n")

        lines.append(f"const stream_meta_t* {modulation}_meta_by_snr[] = "+"{")
        for snr in range(0,snr_generation_range[-1]+1):
            name = f"&complex_{modulation}_{snr}_meta"
            if snr not in snr_generation_range:
                lines.append(f"{PAD}nullptr,")
            else:
                lines.append(f"{PAD}{name},")
        lines.append("};\n")

    lines.append(f"const stream_data_t** data_by_modulation[] = "+"{")
    for modulation in available_modulations:
            name = f"{modulation}_by_snr"
            lines.append(f"{PAD}[{modulation.upper()}] = {name},")
    lines.append("};\n")

    lines.append(f"const stream_meta_t** meta_by_modulation[] = "+"{")
    for modulation in available_modulations:
            name = f"{modulation}_meta_by_snr"
            lines.append(f"{PAD}[{modulation.upper()}] = {name},")
    lines.append("};\n")

    lines.append("#define GET_DATA(modulation,snr) (data_by_modulation[modulation][snr])")
    lines.append("#define GET_META(modulation,snr) (meta_by_modulation[modulation][snr])")
    lines.append("#define GET_MODULATION_NAME(modulation) (modulations_str[modulation])")

    lines.append("#endif")
    
    header_text = "\n".join(lines)
    Path(os.path.join(folder_path,"simulation_base.h")).write_text(header_text)

            
if __name__ == "__main__":
    main()

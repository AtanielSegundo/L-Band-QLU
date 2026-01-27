import warnings
warnings.filterwarnings("ignore", category=UserWarning)

from modulations.bpsk import BpskModem
from modulations.qpsk import QpskModem
from modulations.qam16 import Qam16Modem 
from modulations.util import MegaHz,bytes_to_bits,bits_to_bytes,tx_rx_error

from convert import ToHeaderConverter

def main():
    #stream_str   = open("generate_stream.py","r").read()
    stream_str = "Hello IQ Stream"
    byte_stream = bytearray(stream_str,encoding="ascii")

    link_bw  = MegaHz(83.75)
    link_bw  = MegaHz(51.25)

    sampling_rate = 4 * link_bw
    roll_off = 0.25

    print("[INFO] Starting I/Q Stream Generation")

    bit_stream = bytes_to_bits(byte_stream, msb_first=True)
    
    modems_cls = [BpskModem,QpskModem,Qam16Modem]
    
    Fc     = MegaHz(1105)
    SNR_db = 15.0
    SEED   = 333    
    
    # resolution dictates the bits by float data
    # in iq_t is the real signal, that should be in the resolution range
    # in complex_iq is the same but for each I Q pair, so I[0] is 16 bits and Q[0] is 16 bits too
    h_converter = ToHeaderConverter(resolution=16)

    print("-----------------------------------------------------------------------------")
    for modem_cls in modems_cls:
        modem = modem_cls(sampling_rate,link_bw,roll_off)
        modem_name = type(modem).__name__

        iq_t, complex_iq, info = modem.modulate(bit_stream,fc=Fc,snr_db=SNR_db,seed=SEED)
        _, zer_complex_iq, _ = modem.modulate(bit_stream,fc=0.0,snr_db=SNR_db,seed=SEED)
        zer_eq = sum(complex_iq == zer_complex_iq) / (min(len(complex_iq),len(zer_complex_iq)))
        print("ZERO == CARRIER IQ? ",bool(zer_eq))
        
        modem.plot(f"plots/test_{modem_name}.png",iq_t,complex_iq,info)
        dem_bit_stream = modem.demodulate(iq_t,info)
        dem_bytes = bits_to_bytes(dem_bit_stream,msb_first=True)

        h_name = modem_name.split("Modem")[0].lower()
        h_converter.real_st(iq_t,info,save_path=f"headers/{h_name}.h",
                            arr_name=h_name,
                            elem_per_line=7)
        h_converter.complex_st(complex_iq,info,save_path=f"headers/complex_{h_name}.h",
                               arr_name="complex_"+h_name,
                               elem_per_line=7)

        print(f"{modem_name} Bit rate: {info.bit_rate}")
        print("OK:", dem_bytes == byte_stream)
        print(f"Error: {round(tx_rx_error(byte_stream,dem_bytes) * 100,2)}%")
        print("-----------------------------------------------------------------------------")

if __name__ == "__main__":
    main()

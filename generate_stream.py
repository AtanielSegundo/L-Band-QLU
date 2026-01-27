from modulations.base import ModulationInfo
from modulations.bpsk import BpskModem
from modulations.qpsk import QpskModem
from modulations.qam16 import Qam16Modem 
from modulations.util import MegaHz,bytes_to_bits,bits_to_bytes

def to_8psk(byte_stream, snr_db=30.0, amplitude=1.0, fc=None): 
    pass

def to_16apsk(byte_stream, snr_db=30.0, amplitude=1.0, fc=None): 
    pass

def to_32apsk(byte_stream, snr_db=30.0, amplitude=1.0, fc=None): 
    pass

def to_16QAM(byte_stream, snr_db=30.0, amplitude=1.0, fc=None): 
    pass

def main():
    stream_str   = "Hello IQ Stream"
    byte_stream = bytearray(stream_str,encoding="ascii")

    total_bw = MegaHz(36)
    link_bw  = MegaHz(10)

    sampling_rate = 4 * link_bw
    signal_Resolution = 16 # bits per sample
    roll_off = 0.25

    print("[INFO] Starting I/Q Stream Generation")

    bit_stream = bytes_to_bits(byte_stream, msb_first=True)
    
    modems_cls = [BpskModem,QpskModem,Qam16Modem]
    
    Fc     = 1105e6
    SNR_db = 30.0

    for modem_cls in modems_cls:
        modem = modem_cls(sampling_rate,link_bw,roll_off)
        iq_t, complex_iq, info = modem.modulate(bit_stream,fc=Fc,snr_db=SNR_db)
        modem.plot(f"plots/test_{type(modem).__name__}.png",iq_t,complex_iq,info)
        dem_bit_stream = modem.demodulate(iq_t,info)
        dem_bytes = bits_to_bytes(dem_bit_stream,msb_first=True)

        print(f"{type(modem).__name__} Bit rate: {info.bit_rate}")
        print("TX:", byte_stream)
        print("RX:", dem_bytes)
        print("OK:", dem_bytes == byte_stream)

if __name__ == "__main__":
    main()
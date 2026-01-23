from sys import argv
from matplotlib import pyplot as plt
from pathlib import Path
import numpy as np

MegaHz = lambda v: v * 1e6

stream_str   = "Hello IQ Stream"
stream_bytes = bytearray(stream_str,encoding="ascii")

total_bw = MegaHz(36)
link_bw  = MegaHz(10)

sampling_rate = 2 * link_bw
signal_Resolution = 16 # bits per sample
roll_off = 0.0

def bytes_to_bits(byte_stream, msb_first=True):
    bits = []
    for b in byte_stream:
        for i in range(8):
            if msb_first:
                bits.append((b >> (7-i)) & 1)
            else:
                bits.append((b >> i) & 1)
    return np.array(bits, dtype=np.uint8)

# IQ STREAM
#
#   iq(t) = s(t) + N(mean,std)
#   s(t)  =  cos(2pift) . A(t) . cos[phi(t)]  
#          - sin(2pift) . A(t) . sin[phi(t)]

# SYMBOLS: -1 to 1 
# Replace your to_bpsk with this
def to_bpsk(byte_stream, snr_db=30.0, amplitude=1.0, fc=None):
    global sampling_rate, link_bw, roll_off
    bits = bytes_to_bits(byte_stream, msb_first=True)
    symbols = 1.0 - 2.0 * bits.astype(np.float32)  # +1 or -1
    bit_rate = link_bw / (1.0 + roll_off)

    samples_per_symbol = sampling_rate / bit_rate
    if samples_per_symbol < 2:
        bit_rate = sampling_rate / 4.0
        samples_per_symbol = sampling_rate / bit_rate
    samples_per_symbol = max(1, int(round(samples_per_symbol)))

    symbol_stream = np.repeat(symbols, samples_per_symbol)
    total_samples = symbol_stream.size

    if fc is None:
        fc = link_bw / 4.0

    t = np.arange(total_samples) / sampling_rate

    I_clean = amplitude * symbol_stream.astype(np.float32)
    Q_clean = np.zeros_like(I_clean)

    sig_power = np.mean(I_clean**2 + Q_clean**2)  # baseband power

    # noise power (total complex noise power)
    snr_linear = 10.0**(snr_db / 10.0)
    noise_power = sig_power / snr_linear if sig_power > 0 else 0.0

    # For complex AWGN: variance per real component = noise_power/2
    sigma = np.sqrt(noise_power / 2.0) if noise_power > 0 else 0.0

    # complex noise (real + j*imag) with equal variance
    noise_i = np.random.normal(0.0, sigma, size=I_clean.shape).astype(np.float32)
    noise_q = np.random.normal(0.0, sigma, size=Q_clean.shape).astype(np.float32)
    complex_noise = noise_i + 1j * noise_q

    # noisy baseband (I + jQ) — this is what you should sample for constellation
    complex_baseband_noisy = (I_clean + noise_i) + 1j * (Q_clean + noise_q)

    # build passband from noisy baseband
    I_noisy = (complex_baseband_noisy.real).astype(np.float32)
    Q_noisy = (complex_baseband_noisy.imag).astype(np.float32)
    carrier_cos = np.cos(2.0 * np.pi * fc * t, dtype=np.float32)
    carrier_sin = np.sin(2.0 * np.pi * fc * t, dtype=np.float32)
    # real passband s(t) = I*cos - Q*sin
    s_t = I_noisy * carrier_cos - Q_noisy * carrier_sin

    iq_t = s_t.astype(np.float32)

    meta = {
        "n_bits": int(len(bits)),
        "bit_rate": float(bit_rate),
        "samples_per_symbol": int(samples_per_symbol),
        "total_samples": int(total_samples),
        "carrier_freq": float(fc),
        "snr_db": float(snr_db),
        "noise_std_complex_per_component": float(sigma),
        "sampling_rate": float(sampling_rate),
    }
    
    return iq_t, complex_baseband_noisy.astype(np.complex64), meta

def plot_bpsk_output(iq_t, complex_baseband, meta, samples_to_plot=400, max_nfft=1024, save_figs=False, amplitude=1.0):
    """
    Plota: 1) tempo, 2) constelação, 3) PSD, 4) spectrogram.
    Ajusta NFFT automaticamente para evitar warning quando o sinal é curto.
    """
    fs = float(meta.get("sampling_rate", sampling_rate))
    sps = int(meta.get("samples_per_symbol", 1))
    bit_rate = meta.get("bit_rate", None)
    fc = meta.get("carrier_freq", None)

    # 1) Time-domain zoom
    t = np.arange(len(iq_t)) / fs
    plt.figure(figsize=(10,3))
    plt.plot(t[:samples_to_plot], iq_t[:samples_to_plot])
    plt.title("Time-domain (iq(t)) — zoom")
    plt.xlabel("Time (s)")
    plt.ylabel("Amplitude")
    plt.grid(True)
    plt.tight_layout()
    if save_figs: plt.savefig("bpsk_time.png", dpi=150)

    # 2) Constellation: sample at symbol centers (middle sample of each symbol)
    if sps >= 1:
        centers = np.arange(sps//2, len(complex_baseband), sps)
        if centers.size == 0:
            sym_samples = complex_baseband
        else:
            sym_samples = complex_baseband[centers]

    plt.figure(figsize=(5,5))
    plt.scatter(sym_samples.real, sym_samples.imag, s=12)
    plt.axhline(0, linewidth=0.5)
    plt.axvline(0, linewidth=0.5)
    plt.title("Constellation (sampled symbols)")
    plt.xlabel("I")
    plt.ylabel("Q")
    plt.ylim(-amplitude,amplitude)
    plt.grid(True)
    plt.gca().set_aspect('equal', adjustable='box')
    plt.tight_layout()
    if save_figs: plt.savefig("bpsk_constellation.png", dpi=150)

    # 3) Power Spectral Density (dB) using FFT with automatic nfft
    nfft = min(max_nfft, len(iq_t))
    if nfft < 16:
        nfft = len(iq_t)  # fallback
    X = np.fft.rfft(iq_t * np.hanning(len(iq_t)))
    freqs = np.fft.rfftfreq(len(iq_t), d=1.0/fs)
    psd_db = 20.0 * np.log10(np.abs(X) + 1e-12)
    plt.figure(figsize=(10,3))
    plt.plot(freqs/1e6, psd_db)
    plt.title("PSD (dB)")
    plt.xlabel("Frequency (MHz)")
    plt.ylabel("Magnitude (dB)")
    plt.grid(True)
    plt.tight_layout()
    if save_figs: plt.savefig("bpsk_psd.png", dpi=150)

    # 4) Spectrogram with adjusted NFFT and noverlap
    spec_nfft = min(1024, len(iq_t))
    if spec_nfft < 16:
        spec_nfft = max(4, len(iq_t))
    spec_noverlap = spec_nfft // 2
    plt.figure(figsize=(10,4))
    Pxx, freqs_s, bins, im = plt.specgram(iq_t, NFFT=spec_nfft, Fs=fs, noverlap=spec_noverlap, scale='dB')
    plt.title("Spectrogram (matplotlib.specgram)")
    plt.xlabel("Time (s)")
    plt.ylabel("Frequency (Hz)")
    plt.colorbar(im, label='Intensity (dB)')
    plt.tight_layout()
    if save_figs: plt.savefig("bpsk_spectrogram.png", dpi=150)

    # show all
    plt.show()

def iq_to_c_header(iq_t,
                   out_path="stream.h",
                   array_name="bpsk",
                   sampling_rate=None,
                   signal_resolution=16,
                   guard_name="STREAM_H"):
    """
    iq_t: 1D numpy array (float) — expected dynamic range approx [-1, 1] but works anyway.
    out_path: file path to write the .h
    array_name: C array name
    sampling_rate: integer (Hz) to place in header (if None, omitted)
    signal_resolution: bits (e.g. 16)
    guard_name: include guard macro
    """
    iq = np.asarray(iq_t, dtype=np.float64).copy()
    # compute target unsigned range
    max_uint = (1 << signal_resolution) - 1
    half = max_uint // 2

    # protect against constant-zero signal
    max_abs = np.max(np.abs(iq)) if iq.size > 0 else 0.0
    if max_abs == 0:
        scaled = np.full(iq.shape, half, dtype=np.uint16)
    else:
        # scale to use ±95% of half-range to avoid clipping
        scale = (half * 0.95) / max_abs
        mapped = half + iq * scale
        mapped = np.round(mapped).astype(np.int64)
        mapped = np.clip(mapped, 0, max_uint)
        scaled = mapped.astype(np.uint16)

    # build header text
    lines = []
    lines.append(f"#ifndef {guard_name}")
    lines.append(f"#define {guard_name}")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"typedef uint16_t stream_data_t;")
    lines.append("")
    if sampling_rate is not None:
        lines.append(f"#define SAMPLING_RATE ({int(sampling_rate)})")
    lines.append(f"#define SIGNAL_RESOLUTION ({int(signal_resolution)})")
    lines.append(f"#define {array_name.upper()}_LEN ({scaled.size})")
    lines.append("")
    lines.append(f"static const stream_data_t {array_name}[] = {{")

    # format array values N per line
    N_per_line = 12
    for i in range(0, scaled.size, N_per_line):
        chunk = scaled[i:i+N_per_line]
        chunk_str = ", ".join(str(int(v)) for v in chunk)
        if i + N_per_line >= scaled.size:
            lines.append(f"    {chunk_str}")
        else:
            lines.append(f"    {chunk_str},")
    lines.append("};")
    lines.append("")
    lines.append(f"#endif /* {guard_name} */")
    lines.append("")

    header_text = "\n".join(lines)

    # write file
    Path(out_path).write_text(header_text)
    return header_text

def to_qpsk(byte_stream, snr_db=30.0, amplitude=1.0, fc=None): 
    pass

def to_8psk(byte_stream, snr_db=30.0, amplitude=1.0, fc=None): 
    pass

def to_16apsk(byte_stream, snr_db=30.0, amplitude=1.0, fc=None): 
    pass

def to_32apsk(byte_stream, snr_db=30.0, amplitude=1.0, fc=None): 
    pass

def to_16QAM(byte_stream, snr_db=30.0, amplitude=1.0, fc=None): 
    pass

def main():
    print("[INFO] Starting I/Q Stream Generation")
    print("\n")
    print(f"[INFO] Stream String: {stream_str}")
    print(f"[INFO] Stream Bytes ({len(stream_bytes)} Bytes):")
    for b in stream_bytes:
        print(f"\t{hex(b)} = '{chr(b)}'")
    
    iq_t, complex, meta = to_bpsk(stream_bytes,snr_db=30.0,amplitude=1.0)
    iq_to_c_header(iq_t)
    plot_bpsk_output(iq_t, complex, meta, samples_to_plot=400)
    
    
if __name__ == "__main__":
    main()
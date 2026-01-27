import numpy as np
from modulations.base import * 
from matplotlib import pyplot as plt
from typing import *

class Qam16Modem:
    """
    16-QAM (Gray coded, rectangular -3,-1,+1,+3 per eixo).
    Bits por símbolo = 4.
    Normalização para potência média por símbolo = 1 (norm = 1/sqrt(10)).
    """
    def __init__(self, sampling_rate, link_bw, roll_off):
        self.sampling_rate = sampling_rate
        self.link_bw = link_bw
        self.roll_off = roll_off

    def _bits_to_level(self, b0: int, b1: int) -> int:
        # Gray mapping (2 bits -> level index): 00->-3, 01->-1, 11->+1, 10->+3
        if b0 == 0 and b1 == 0:
            return -3
        if b0 == 0 and b1 == 1:
            return -1
        if b0 == 1 and b1 == 1:
            return +1
        return +3

    def _level_to_bits(self, level: int) -> Tuple[int,int]:
        # inverse of above
        if level == -3:
            return (0,0)
        if level == -1:
            return (0,1)
        if level == +1:
            return (1,1)
        return (1,0)

    def modulate(self, bit_stream, snr_db=30.0, amplitude=1.0, fc=None) -> Tuple[List[float], List[complex], ModulationInfo]:
        fs = float(self.sampling_rate)
        B = float(self.link_bw)
        alpha = float(self.roll_off)
        
        if fc is None:
            fc = B / 4.0

        bits = np.asarray(bit_stream, dtype=np.uint8)

        # pad to multiple of 4
        if len(bits) % 4 != 0:
            pad = 4 - (len(bits) % 4)
            bits = np.concatenate([bits, np.zeros(pad, dtype=np.uint8)])

        # symbol rate (same Rs for given bandwidth)
        symbol_rate = B / (1.0 + alpha)

        # samples per symbol (integer)
        sps = int(np.ceil(fs / symbol_rate))
        if sps < 1:
            sps = 1

        # realign symbol_rate to integer sps
        symbol_rate = fs / sps
        bit_rate = 4.0 * symbol_rate

        n_symbols = len(bits) // 4

        # normalization for average symbol power = 1
        norm = 1.0 / np.sqrt(10.0)

        I_levels = np.zeros(n_symbols, dtype=np.float32)
        Q_levels = np.zeros(n_symbols, dtype=np.float32)

        for i in range(n_symbols):
            b0 = int(bits[4*i + 0])
            b1 = int(bits[4*i + 1])
            b2 = int(bits[4*i + 2])
            b3 = int(bits[4*i + 3])

            I_lv = self._bits_to_level(b0, b1)
            Q_lv = self._bits_to_level(b2, b3)

            I_levels[i] = I_lv * norm
            Q_levels[i] = Q_lv * norm

        # upsample
        I_stream = np.repeat(I_levels, sps)
        Q_stream = np.repeat(Q_levels, sps)

        total_samples = I_stream.size
        t = np.arange(total_samples) / fs

        I_clean = amplitude * I_stream.astype(np.float32)
        Q_clean = amplitude * Q_stream.astype(np.float32)

        sig_power = np.mean(I_clean**2 + Q_clean**2)

        # AWGN (complex)
        snr_linear = 10.0**(snr_db / 10.0)
        noise_power = sig_power / snr_linear if sig_power > 0 else 0.0
        sigma = np.sqrt(noise_power / 2.0) if noise_power > 0 else 0.0

        noise_i = np.random.normal(0.0, sigma, size=I_clean.shape).astype(np.float32)
        noise_q = np.random.normal(0.0, sigma, size=Q_clean.shape).astype(np.float32)

        complex_baseband_noisy = (I_clean + noise_i) + 1j * (Q_clean + noise_q)

        # if fc == 0 return complex baseband (useful for SDR/baseband processing)
        if fc == 0.0:
            iq_t = complex_baseband_noisy.astype(np.complex64)
        else:
            # Modulate onto carrier
            carrier_cos = np.cos(2.0 * np.pi * fc * t, dtype=np.float32)
            carrier_sin = np.sin(2.0 * np.pi * fc * t, dtype=np.float32)
            s_t = (complex_baseband_noisy.real * carrier_cos
                - complex_baseband_noisy.imag * carrier_sin)
            iq_t = s_t.astype(np.float32)

        info = ModulationInfo(
            len(bits),
            bit_rate,
            sps,
            total_samples,
            fc,
            snr_db,
            sigma,
            int(fs)
        )

        return iq_t, complex_baseband_noisy.astype(np.complex64), info

    def demodulate(self, stream: List[float], info: ModulationInfo, debug=False,enable_phase_corr=False) -> List[int]:
        n_bits = info.n_bits
        fs = float(info.sampling_rate)
        fc = info.carrier_freq
        sps = int(info.samples_per_symbol)

        # --- get complex baseband (bb) ---
        if np.iscomplexobj(stream):
            # already complex baseband (fc == 0 path)
            bb = np.asarray(stream, dtype=np.complex64)
            need_double = False
        else:
            # real passband: mix down to complex and then low-pass (we'll *double* after LPF)
            iq_t = np.asarray(stream, dtype=np.float32)
            n = np.arange(len(iq_t))
            t = n / fs
            lo = np.exp(-1j * 2.0 * np.pi * fc * t).astype(np.complex64)
            bb = iq_t.astype(np.complex64) * lo
            need_double = True

        if len(bb) == 0:
            return np.zeros(0, dtype=np.uint8)

        # --- matched filter (rectangular) using 'same' to avoid manual full-offset math ---
        h = np.ones(sps, dtype=np.float32) / float(sps)
        bb_f = np.convolve(bb, h.astype(np.complex64), mode="same")

        # If we mixed down from a real passband, recover full complex envelope:
        if need_double:
            bb_f = bb_f * 2.0

        # pick symbol centers (same convention used in TX)
        centers = np.arange(sps // 2, len(bb_f), sps).astype(int)
        centers = centers[centers < len(bb_f)]
        if centers.size == 0:
            return np.zeros(0, dtype=np.uint8)

        sym = bb_f[centers]

        # FIX: Make phase correction optional
        if enable_phase_corr:
            try:
                # Note: This requires many symbols to be accurate!
                phi = np.angle(np.mean(sym ** 4)) / 4.0
                sym = sym * np.exp(-1j * phi)
            except Exception:
                pass

        # limit to expected number of symbols (4 bits / symbol)
        n_symbols = min(len(sym), n_bits // 4)
        if n_symbols <= 0:
            return np.zeros(0, dtype=np.uint8)

        I_sym = sym[:n_symbols].real
        Q_sym = sym[:n_symbols].imag

        if debug:
            print(f"[demod] fs={fs}, fc={fc}, sps={sps}, centers={centers.tolist()}")
            print(f"[demod] n_symbols={n_symbols}, first sym (I,Q):", [(float(I_sym[i]), float(Q_sym[i])) for i in range(min(6, n_symbols))])

        # decision: nearest of levels [-3,-1,1,3] with same normalization used no TX
        norm = 1.0 / np.sqrt(10.0)
        levels = np.array([-3, -1, 1, 3], dtype=np.int32)
        bits_hat = np.zeros(n_symbols * 4, dtype=np.uint8)

        for i in range(n_symbols):
            Iv = I_sym[i] / norm
            Qv = Q_sym[i] / norm
            I_lv = levels[np.argmin(np.abs(levels - Iv))]
            Q_lv = levels[np.argmin(np.abs(levels - Qv))]

            # map level -> bits (same mapping from TX)
            if I_lv == -3: bI0, bI1 = 0, 0
            elif I_lv == -1: bI0, bI1 = 0, 1
            elif I_lv == 1: bI0, bI1 = 1, 1
            else: bI0, bI1 = 1, 0

            if Q_lv == -3: bQ0, bQ1 = 0, 0
            elif Q_lv == -1: bQ0, bQ1 = 0, 1
            elif Q_lv == 1: bQ0, bQ1 = 1, 1
            else: bQ0, bQ1 = 1, 0

            bits_hat[4*i + 0] = bI0
            bits_hat[4*i + 1] = bI1
            bits_hat[4*i + 2] = bQ0
            bits_hat[4*i + 3] = bQ1

        return bits_hat[:n_bits]


    def plot(self, save_path: str, iq_t, complex_baseband, info: ModulationInfo,
             show=False, max_nfft=1024, amplitude=1.0):
        fs = info.sampling_rate
        sps = info.samples_per_symbol

        fig = plt.figure(figsize=(16, 10))
        gs = fig.add_gridspec(3, 2, height_ratios=[1.5, 1.5, 1], hspace=0.3, wspace=0.3)

        # 1) Time-domain (top left)
        ax1 = fig.add_subplot(gs[0, 0])
        t = np.arange(len(iq_t)) / fs
        
        # Handle complex time domain plotting
        if np.iscomplexobj(iq_t):
            ax1.plot(t, iq_t.real, label='Real (I)')
            ax1.plot(t, iq_t.imag, label='Imag (Q)', alpha=0.7)
            ax1.legend(loc='upper right', fontsize='small')
        else:
            ax1.plot(t, iq_t)
            
        ax1.set_title(f"Time-domain (16-QAM) [{'Baseband' if np.iscomplexobj(iq_t) else 'Passband'}]")
        ax1.set_xlabel("Time (s)")
        ax1.set_ylabel("Amplitude")
        ax1.grid(True)

        # 2) Constellation (top right - BIGGEST)
        ax2 = fig.add_subplot(gs[0:2, 1])
        if sps >= 1:
            centers = np.arange(sps//2, len(complex_baseband), sps)
            sym_samples = complex_baseband[centers] if centers.size != 0 else complex_baseband
        else:
            sym_samples = complex_baseband

        ax2.scatter(sym_samples.real, sym_samples.imag, s=14, alpha=0.6)
        ax2.axhline(0, linewidth=0.5, color='black')
        ax2.axvline(0, linewidth=0.5, color='black')

        # ideal points
        norm = 1.0 / np.sqrt(10.0)
        levels = np.array([-3, -1, 1, 3]) * norm * amplitude
        pts = np.array([[i, q] for i in levels for q in levels])
        ax2.scatter(pts[:,0], pts[:,1], s=100, marker='x', color='red', 
                    linewidths=2, label='Ideal', zorder=10)
        ax2.set_title("Constellation - 16-QAM (sampled symbols)", fontsize=14, fontweight='bold')
        ax2.set_xlabel("I")
        ax2.set_ylabel("Q")
        ax2.set_ylim(-amplitude*1.5, amplitude*1.5)
        ax2.set_xlim(-amplitude*1.5, amplitude*1.5)
        ax2.set_aspect('equal', adjustable='box')
        ax2.grid(True, alpha=0.3)
        ax2.legend()

        # 3) Power Spectral Density (middle left)
        # FIX: Check if input is Real or Complex to choose the correct FFT
        ax3 = fig.add_subplot(gs[1, 0])
        nfft = min(max_nfft, len(iq_t))
        if nfft < 16:
            nfft = len(iq_t)
            
        window = np.hanning(len(iq_t))
        
        if np.iscomplexobj(iq_t):
            # Complex Baseband: Use standard FFT and shift 0Hz to center
            X = np.fft.fft(iq_t * window)
            X = np.fft.fftshift(X)
            freqs = np.fft.fftfreq(len(iq_t), d=1.0/fs)
            freqs = np.fft.fftshift(freqs)
        else:
            # Real Passband: Use Real FFT (0 to fs/2)
            X = np.fft.rfft(iq_t * window)
            freqs = np.fft.rfftfreq(len(iq_t), d=1.0/fs)

        psd_db = 20.0 * np.log10(np.abs(X) + 1e-12)
        ax3.plot(freqs/1e6, psd_db)
        ax3.set_title("PSD (dB)")
        ax3.set_xlabel("Frequency (MHz)")
        ax3.set_ylabel("Magnitude (dB)")
        ax3.grid(True)

        # 4) Spectrogram (bottom - SMALLEST)
        ax4 = fig.add_subplot(gs[2, :])
        spec_nfft = min(1024, len(iq_t))
        if spec_nfft < 16:
            spec_nfft = max(4, len(iq_t))
        spec_noverlap = spec_nfft // 2
        
        # Matplotlib specgram handles complex inputs automatically (showing negative freqs)
        Pxx, freqs_s, bins, im = ax4.specgram(iq_t, NFFT=spec_nfft, Fs=fs, noverlap=spec_noverlap, scale='dB')
        ax4.set_title("Spectrogram")
        ax4.set_xlabel("Time (s)")
        ax4.set_ylabel("Frequency (Hz)")
        plt.colorbar(im, ax=ax4, label='Intensity (dB)')

        plt.savefig(save_path, dpi=150, bbox_inches='tight')
        print(f"Figure saved to: {save_path}")
        
        if show:
            plt.show()
        plt.close(fig) # Close to free memory
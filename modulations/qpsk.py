import numpy as np
from modulations.base import * 
from matplotlib import pyplot as plt
from typing import *

'''
[Ataniel - 26/01/2026]

[QPSK]
    --- N = 4 símbolos (2 bits por símbolo)
    --- Constelação: 4 pontos nas fases 45°, 135°, 225°, 315°
    --- Gray Coding Mapping:
        00 → (I=+1, Q=+1) → fase 45°
        01 → (I=-1, Q=+1) → fase 135°  
        11 → (I=-1, Q=-1) → fase 225°
        10 → (I=+1, Q=-1) → fase 315°
    --- A(t) = sqrt(2*Es/T) ... Es = Energia do simbolo e T = Periodo do simbolo
    --- f{c} ~= Frequencia da portadora
'''

class QpskModem:
    def __init__(self, sampling_rate, link_bw, roll_off):
        self.sampling_rate = sampling_rate
        self.link_bw       = link_bw
        self.roll_off      = roll_off
    
    def modulate(self, bit_stream, snr_db=30.0, amplitude=1.0, fc=None, seed= None) -> Tuple[List[float], List[complex], ModulationInfo]:
        sampling_rate = self.sampling_rate
        link_bw       = self.link_bw
        roll_off      = self.roll_off
        
        if fc is None:
            fc = link_bw / 4.0

        bits = bit_stream
        
        if seed is not None:
            np.random.seed(seed)

        # QPSK: 2 bits per symbol
        # Pad bits to make even length
        if len(bits) % 2 != 0:
            bits = np.append(bits, 0)
        
        # Symbol rate is half of bit rate (2 bits/symbol)
        symbol_rate = link_bw / (1.0 + roll_off)
        samples_per_symbol = sampling_rate / symbol_rate
        samples_per_symbol = max(1, int(np.ceil(samples_per_symbol)))
        
        # Group bits into pairs (dibits) and map to QPSK symbols
        # Gray coding: 00→(+1,+1), 01→(-1,+1), 11→(-1,-1), 10→(+1,-1)
        n_symbols = len(bits) // 2
        I_symbols = np.zeros(n_symbols, dtype=np.float32)
        Q_symbols = np.zeros(n_symbols, dtype=np.float32)
        
        # Normalization factor to maintain unit energy
        norm = 1.0 / np.sqrt(2.0)
        
        for i in range(n_symbols):
            bit0 = bits[2*i]      # First bit
            bit1 = bits[2*i + 1]  # Second bit
            
            # Gray coding mapping
            if bit0 == 0 and bit1 == 0:      # 00
                I_symbols[i] = norm
                Q_symbols[i] = norm
            elif bit0 == 0 and bit1 == 1:    # 01
                I_symbols[i] = -norm
                Q_symbols[i] = norm
            elif bit0 == 1 and bit1 == 1:    # 11
                I_symbols[i] = -norm
                Q_symbols[i] = -norm
            else:                             # 10
                I_symbols[i] = norm
                Q_symbols[i] = -norm
        
        # Upsample symbols to match sampling rate
        I_stream = np.repeat(I_symbols, samples_per_symbol)
        Q_stream = np.repeat(Q_symbols, samples_per_symbol)
        
        total_samples = I_stream.size
        t = np.arange(total_samples) / sampling_rate
        
        # Apply amplitude
        I_clean = amplitude * I_stream.astype(np.float32)
        Q_clean = amplitude * Q_stream.astype(np.float32)
        
        # Calculate signal power
        sig_power = np.mean(I_clean**2 + Q_clean**2)
        
        # Add AWGN noise
        snr_linear = 10.0**(snr_db / 10.0)
        noise_power = sig_power / snr_linear if sig_power > 0 else 0.0
        sigma = np.sqrt(noise_power / 2.0) if noise_power > 0 else 0.0
        
        noise_i = np.random.normal(0.0, sigma, size=I_clean.shape).astype(np.float32)
        noise_q = np.random.normal(0.0, sigma, size=Q_clean.shape).astype(np.float32)
        
        # Complex baseband signal with noise
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
        
        # Create modulation info
        info = ModulationInfo(
            len(bits),
            symbol_rate * 2.0,  # bit_rate = symbol_rate * bits_per_symbol
            samples_per_symbol,
            total_samples,
            fc,
            snr_db,
            sigma,
            sampling_rate
        )
        
        return iq_t, complex_baseband_noisy.astype(np.complex64), info
    
    def demodulate(self, qpsk_stream:List[float], info:ModulationInfo, amplitude=1.0) -> List[int]:
        n_bits = info.n_bits
        fs     = info.sampling_rate
        fc     = info.carrier_freq
        sps    = info.samples_per_symbol
        
        if np.iscomplexobj(qpsk_stream):
            # already complex baseband (fc == 0 path)
            bb = np.asarray(qpsk_stream, dtype=np.complex64)
            need_double = False
        else:
            # real passband: mix down to complex and then low-pass (we'll *double* after LPF)
            iq_t = np.asarray(qpsk_stream, dtype=np.float32)
            n = np.arange(len(iq_t))
            t = n / fs
            lo = np.exp(-1j * 2.0 * np.pi * fc * t).astype(np.complex64)
            bb = iq_t.astype(np.complex64) * lo
            need_double = True

        if len(bb) == 0:
            return np.zeros(0, dtype=np.uint8)

        # === LPF ===
        h = np.ones(sps, dtype=np.float32) / sps
        bb_f = np.convolve(bb, h.astype(np.complex64), mode="same")
        
        if need_double:
            bb_f = bb_f * 2.0

        # Sample at symbol centers
        centers = np.arange(sps // 2, len(bb_f), sps)
        centers = centers[centers < len(bb_f)]
        if centers.size == 0:
            return np.zeros(0, dtype=np.uint8)
        
        sym = bb_f[centers]
        # Number of symbols (2 bits per symbol)
        n_symbols = min(len(sym), n_bits // 2)
        
        # Decision regions for QPSK (Gray coding)
        bits_hat = np.zeros(n_symbols * 2, dtype=np.uint8)
        if n_symbols <= 0:
            return np.zeros(0, dtype=np.uint8)
        
        I_sym = sym[:n_symbols].real
        Q_sym = sym[:n_symbols].imag

        for i in range(n_symbols):
            I_val = I_sym[i]
            Q_val = Q_sym[i]
            
            # Decode based on quadrant (Gray coding)
            if I_val >= 0 and Q_val >= 0:      # Quadrant I: 00
                bits_hat[2*i] = 0
                bits_hat[2*i + 1] = 0
            elif I_val < 0 and Q_val >= 0:     # Quadrant II: 01
                bits_hat[2*i] = 0
                bits_hat[2*i + 1] = 1
            elif I_val < 0 and Q_val < 0:      # Quadrant III: 11
                bits_hat[2*i] = 1
                bits_hat[2*i + 1] = 1
            else:                               # Quadrant IV: 10
                bits_hat[2*i] = 1
                bits_hat[2*i + 1] = 0
        
        # Trim to original bit length
        bits_hat = bits_hat[:n_bits]
        
        return bits_hat
    
    def plot(self, save_path:str, iq_t, complex_baseband, info:ModulationInfo,  
        show=False,
        max_nfft=1024, 
        amplitude=1.0
    ):
        """
        Plota: 1) tempo, 2) constelação (maior), 3) PSD, 4) spectrogram (menor).
        Ajusta NFFT automaticamente para evitar warning quando o sinal é curto.
        Todos os plots em uma única figura com layout customizado.
        """
        fs       = info.sampling_rate
        sps      = info.samples_per_symbol
        
        # Create figure with custom grid layout
        fig = plt.figure(figsize=(16, 10))
        gs = fig.add_gridspec(3, 2, height_ratios=[1.5, 1.5, 1], hspace=0.3, wspace=0.3)
        
        # 1) Time-domain (top left)
        ax1 = fig.add_subplot(gs[0, 0])
        t = np.arange(len(iq_t)) / fs
        
        if np.iscomplexobj(iq_t):
            ax1.plot(t, iq_t.real, label='Real (I)')
            ax1.plot(t, iq_t.imag, label='Imag (Q)', alpha=0.7)
            ax1.legend(loc='upper right', fontsize='small')
        else:
            ax1.plot(t, iq_t)

        ax1.set_title(f"Time-domain (QPSK) [{'Baseband' if np.iscomplexobj(iq_t) else 'Passband'}]")
        ax1.set_xlabel("Time (s)")
        ax1.set_ylabel("Amplitude")
        ax1.grid(True)

        # 2) Constellation (top right - BIGGEST)
        ax2 = fig.add_subplot(gs[0:2, 1])  # Spans 2 rows
        if sps >= 1:
            centers = np.arange(sps//2, len(complex_baseband), sps)
            if centers.size == 0:
                sym_samples = complex_baseband
            else:
                sym_samples = complex_baseband[centers]
        else:
            sym_samples = complex_baseband
            
        ax2.scatter(sym_samples.real, sym_samples.imag, s=12, alpha=0.6)
        ax2.axhline(0, linewidth=0.5, color='black')
        ax2.axvline(0, linewidth=0.5, color='black')
        
        # Add ideal constellation points
        norm = 1.0 / np.sqrt(2.0)
        ideal_points = np.array([
            [norm, norm],    # 00
            [-norm, norm],   # 01
            [-norm, -norm],  # 11
            [norm, -norm]    # 10
        ]) * amplitude
        ax2.scatter(ideal_points[:, 0], ideal_points[:, 1], 
                   s=100, marker='x', color='red', linewidths=2, 
                   label='Ideal', zorder=10)
        
        ax2.set_title("Constellation - QPSK", fontsize=14, fontweight='bold')
        ax2.set_xlabel("I")
        ax2.set_ylabel("Q")
        ax2.set_ylim(-amplitude*1.5, amplitude*1.5)
        ax2.set_xlim(-amplitude*1.5, amplitude*1.5)
        ax2.grid(True, alpha=0.3)
        ax2.set_aspect('equal', adjustable='box')
        ax2.legend()

        # 3) Power Spectral Density (middle left)
        ax3 = fig.add_subplot(gs[1, 0])
        nfft = min(max_nfft, len(iq_t))
        if nfft < 16:
            nfft = len(iq_t)  # fallback
        
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
        ax4 = fig.add_subplot(gs[2, :])  # Spans full width
        spec_nfft = min(1024, len(iq_t))
        if spec_nfft < 16:
            spec_nfft = max(4, len(iq_t))
        spec_noverlap = spec_nfft // 2
        Pxx, freqs_s, bins, im = ax4.specgram(iq_t, NFFT=spec_nfft, Fs=fs, noverlap=spec_noverlap, scale='dB')
        ax4.set_title("Spectrogram")
        ax4.set_xlabel("Time (s)")
        ax4.set_ylabel("Frequency (Hz)")
        cbar = plt.colorbar(im, ax=ax4, label='Intensity (dB)')

        plt.savefig(save_path, dpi=150, bbox_inches='tight')
        print(f"Figure saved to: {save_path}")
        
        if show:
            plt.show()
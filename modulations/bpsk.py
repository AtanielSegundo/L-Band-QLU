# BPSK.py

import numpy as np
from modulations.base import * 
from matplotlib import pyplot as plt
from typing import *

class BpskModem:
    def __init__(self,sampling_rate, link_bw, roll_off):
        self.sampling_rate = sampling_rate
        self.link_bw       = link_bw
        self.roll_off      = roll_off
    
    def modulate(self,bit_stream, snr_db=30.0, amplitude=1.0, fc=None, seed=None) -> Tuple[List[float],List[complex],ModulationInfo]:
        sampling_rate = self.sampling_rate
        link_bw       = self.link_bw
        roll_off      = self.roll_off
        fc            = fc or link_bw/4.0

        bits = bit_stream
        symbol_rate = link_bw / (1.0 + roll_off)

        if seed is not None:
            np.random.seed(seed)
            
        # I/Q is: +1 or -1 => O{i} = 0 or pi
        symbols = 1.0 - 2.0 * bits.astype(np.float32)  
        samples_per_symbol = sampling_rate / symbol_rate
        samples_per_symbol = max(1, int(np.ceil(samples_per_symbol)))
            
        symbol_rate = sampling_rate / samples_per_symbol
        bit_rate = symbol_rate
        
        symbol_stream = np.repeat(symbols, samples_per_symbol)
        total_samples = symbol_stream.size
        t = np.arange(total_samples) / sampling_rate

        I_clean = amplitude * symbol_stream.astype(np.float32)
        Q_clean = np.zeros_like(I_clean)

        sig_power = np.mean(I_clean**2 + Q_clean**2)

        snr_linear = 10.0**(snr_db / 10.0)
        noise_power = sig_power / snr_linear if sig_power > 0 else 0.0
        sigma = np.sqrt(noise_power / 2.0) if noise_power > 0 else 0.0

        noise_i = np.random.normal(0.0, sigma, size=I_clean.shape).astype(np.float32)
        noise_q = np.random.normal(0.0, sigma, size=Q_clean.shape).astype(np.float32)

        complex_baseband_noisy = (I_clean + noise_i) + 1j * (Q_clean + noise_q)

        I_noisy = (complex_baseband_noisy.real).astype(np.float32)
        Q_noisy = (complex_baseband_noisy.imag).astype(np.float32)

        carrier_cos = np.cos(2.0 * np.pi * fc * t, dtype=np.float32)
        carrier_sin = np.sin(2.0 * np.pi * fc * t, dtype=np.float32)

        s_t = I_noisy * carrier_cos - Q_noisy * carrier_sin

        iq_t = s_t.astype(np.float32)

        info = ModulationInfo(len(bits), bit_rate, samples_per_symbol, total_samples, fc, snr_db, sigma, sampling_rate)
        
        return iq_t, complex_baseband_noisy.astype(np.complex64), info

    def demodulate(self,bpsk_stream:List[float],info:ModulationInfo, amplitude=1.0, msb_first=True) -> List[int]:
        n_bits = info.n_bits
        fs  = info.sampling_rate
        fc  = info.carrier_freq
        sps = info.samples_per_symbol

        iq_t = np.asarray(bpsk_stream, dtype=np.float32)
        n = np.arange(len(iq_t))
        t = n / fs

        cos_c = np.cos(2.0 * np.pi * fc * t, dtype=np.float32)
        sin_c = np.sin(2.0 * np.pi * fc * t, dtype=np.float32)

        I = 2.0 * iq_t * cos_c
        Q = -2.0 * iq_t * sin_c 

        # filtro casado + LPF
        h = np.ones(sps, dtype=np.float32) / sps
        I = np.convolve(I, h, mode="same")
        Q = np.convolve(Q, h, mode="same")

        centers = np.arange(sps // 2, len(I), sps)
        I_sym = I[centers]

        '''
            Slicer Part

            -A => bit 0 
             A => bit 1
        '''

        bits_hat = (I_sym < 0).astype(np.uint8)
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
        
        # 1) Time-domain zoom (top left)
        ax1 = fig.add_subplot(gs[0, 0])
        t = np.arange(len(iq_t)) / fs
        ax1.plot(t, iq_t)
        ax1.set_title("Time-domain")
        ax1.set_xlabel("Time (s)")
        ax1.set_ylabel("Amplitude")
        ax1.grid(True)

        # 2) Constellation (top right - BIGGEST)
        ax2 = fig.add_subplot(gs[0:2, 1])  # Spans 2 rows

        # Add ideal constellation points
        norm = 1.0
        ideal_points = np.array([
            [norm, 0],    # 0
            [-norm,0],   # 1
        ]) * amplitude
        ax2.scatter(ideal_points[:, 0], ideal_points[:, 1], 
                   s=100, marker='x', color='red', linewidths=2, 
                   zorder=10)
        
        if sps >= 1:
            centers = np.arange(sps//2, len(complex_baseband), sps)
            if centers.size == 0:
                sym_samples = complex_baseband
            else:
                sym_samples = complex_baseband[centers]
        else:
            sym_samples = complex_baseband
            
        ax2.scatter(sym_samples.real, sym_samples.imag, s=12)
        ax2.axhline(0, linewidth=0.5, color='black')
        ax2.axvline(0, linewidth=0.5, color='black')
        ax2.set_title("Constellation - BPSK", fontsize=14, fontweight='bold')
        ax2.set_xlabel("I")
        ax2.set_ylabel("Q")
        ax2.set_ylim(-amplitude*1.5, amplitude*1.5)
        ax2.set_xlim(-amplitude*1.5, amplitude*1.5)
        ax2.grid(True)
        ax2.set_aspect('equal', adjustable='box')

        # 3) Power Spectral Density (middle left)
        ax3 = fig.add_subplot(gs[1, 0])
        nfft = min(max_nfft, len(iq_t))
        if nfft < 16:
            nfft = len(iq_t)  # fallback
        X = np.fft.rfft(iq_t * np.hanning(len(iq_t)))
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
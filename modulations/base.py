'''

[Ataniel - 22/01/2026]

IQ STREAM

iq(t) = s(t) + N(mean,std) ... N ~ Distribuição Normal (Ruido Termico + Ruido do meio)

s(t) = cos(2pif{c}t) . A(t) . cos[phi(t)] - sin(2pif{c}t) . A(t) . sin[phi(t)]

I(t) = cos(2pif{c}t) . A(t) . cos[phi(t)]
Q(t) = sin(2pif{c}t) . A(t) . sin[phi(t)]

[Ataniel - 26/01/2026]
    --- A(t) = sqrt(2*Es/T) ... Es = Energia do simbolo e T = Periodo do simbolo
    --- phi(t) := phi[i] ... phi[i] é uma das N possiveis fases
    --- N ~= Numero de simbolos ... log2(N) = m bits/simbolo 
                                            (e.g: BPSK => N=2 => m = 1 bit/simbolo)
    --- f{c} ~= Frequencia da portadora 

SNR = B * (C/N0)

'''

class ModulationInfo:
    def __init__(self,n_bits,bit_rate,samples_per_symbol,total_samples,carrier_freq,snr_db,noise_std_complex_per_component,sampling_rate):
        self.n_bits                          = int(n_bits)
        self.bit_rate                        = float(bit_rate)
        self.samples_per_symbol              = int(samples_per_symbol)
        self.total_samples                   = int(total_samples)
        self.carrier_freq                    = float(carrier_freq)
        self.snr_db                          = float(snr_db)
        self.noise_std_complex_per_component = float(noise_std_complex_per_component)
        self.sampling_rate                   = float(sampling_rate)
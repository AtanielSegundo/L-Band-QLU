import numpy as np

MegaHz = lambda v: v * 1e6

def bytes_to_bits(byte_stream, msb_first=True):
    bits = []
    for b in byte_stream:
        for i in range(8):
            if msb_first:
                bits.append((b >> (7-i)) & 1)
            else:
                bits.append((b >> i) & 1)
    return np.array(bits, dtype=np.uint8)

def bits_to_bytes(bits, msb_first=True):
    bits = np.asarray(bits, dtype=np.uint8)
    n = (len(bits) // 8) * 8
    bits = bits[:n]

    bytes_out = []
    for i in range(0, n, 8):
        b = 0
        for k in range(8):
            if msb_first:
                b = (b << 1) | int(bits[i + k])
            else:
                b |= int(bits[i + k]) << k
        bytes_out.append(b)

    return bytearray(bytes_out)

import numpy as np
from pathlib import Path

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
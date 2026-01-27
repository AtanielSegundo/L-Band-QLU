import numpy as np
from modulations.base import ModulationInfo
from pathlib import Path

class ToHeaderConverter:
    def __init__(self, resolution: int):
        self.resolution = resolution
        if resolution % 8 != 0:
            print(f"[WARNING] Maybe Resolution {self.resolution} is not supported")
        self.stream_data_t = f"uint{resolution}_t"

    def _scale_to_uint(self, x: np.ndarray):
        x = np.asarray(x, dtype=np.float64)

        max_uint = (1 << self.resolution) - 1
        half = max_uint // 2

        max_abs = np.max(np.abs(x)) if x.size > 0 else 0.0
        if max_abs == 0:
            scaled = np.full(x.shape, half, dtype=np.uint16)
            scale = 1.0
        else:
            scale = (half * 0.95) / max_abs
            mapped = half + x * scale
            mapped = np.round(mapped).astype(np.int64)
            mapped = np.clip(mapped, 0, max_uint)
            scaled = mapped.astype(np.uint16)

        return scaled, scale

    def write_stream_meta_data(self,lines,arr_name,n_samples,scale,info:ModulationInfo):        
        lines.append("#ifndef STREAM_METADATA_TYPE")
        lines.append("#define STREAM_METADATA_TYPE")
        lines.append("typedef struct {")
        lines.append("    uint32_t sampling_rate;")
        lines.append("    uint32_t signal_resolution;")
        lines.append("    float carrier_freq;")
        lines.append("    float samples_per_symbol;")
        lines.append("    uint32_t n_samples;")
        lines.append("    float scale;")
        lines.append("} stream_meta_t;")
        lines.append("#endif")
        lines.append("")

        lines.append(f"static const stream_meta_t {arr_name}_meta = {{")
        lines.append(f"    .sampling_rate = {int(info.sampling_rate)},")
        lines.append(f"    .signal_resolution = {int(self.resolution)},")
        lines.append(f"    .carrier_freq = {float(info.carrier_freq)},")
        lines.append(f"    .samples_per_symbol = {float(info.samples_per_symbol)},")
        lines.append(f"    .n_samples = {int(n_samples)},")
        lines.append(f"    .scale = {float(scale)},")
        lines.append("};")
        lines.append("")
    
    def write_stream_data_type(self,lines):
        lines.append(f"#ifndef STREAM_DATA_TYPE")
        lines.append(f"#define STREAM_DATA_TYPE")
        lines.append(f"typedef {self.stream_data_t} stream_data_t;")
        lines.append(f"#endif")
        lines.append("")

    def real_st(self,st,info:ModulationInfo,save_path:str,
                arr_name="stream",elem_per_line=12):
        iq = np.asarray(st, dtype=np.float64).copy()
        max_uint = (1 << self.resolution) - 1
        half = max_uint // 2

        max_abs = np.max(np.abs(iq)) if iq.size > 0 else 0.0
        if max_abs == 0:
            scaled = np.full(iq.shape, half, dtype=np.uint16)
        else:
            scale = (half * 0.95) / max_abs
            mapped = half + iq * scale
            mapped = np.round(mapped).astype(np.int64)
            mapped = np.clip(mapped, 0, max_uint)
            scaled = mapped.astype(np.uint16)

        header_prefix = arr_name.upper()
        guard_name = header_prefix+"_H"

        lines = []
        lines.append(f"#ifndef {guard_name}")
        lines.append(f"#define {guard_name}")
        lines.append("")
        lines.append("#include <stdint.h>")
        lines.append("")
        
        self.write_stream_data_type(lines)
        self.write_stream_meta_data(lines,arr_name,iq.size,scale,info)

        lines.append("")
        lines.append(f"static const stream_data_t {arr_name}[] = {{")

        for i in range(0, scaled.size, elem_per_line):
            chunk = scaled[i:i+elem_per_line]
            chunk_str = ", ".join(str(int(v)) for v in chunk)
            if i + elem_per_line >= scaled.size:
                lines.append(f"    {chunk_str}")
            else:
                lines.append(f"    {chunk_str},")
        lines.append("};")
        lines.append("")
        lines.append(f"#endif /* {guard_name} */")
        lines.append("")

        header_text = "\n".join(lines)

        Path(save_path).write_text(header_text)


    def complex_st(self,
                   complex_st,
                   info: ModulationInfo,
                   save_path: str,
                   arr_name="iq_stream",
                   elem_per_line=12):

        z = np.asarray(complex_st, dtype=np.complex128)

        I = z.real
        Q = z.imag

        # mesma escala para I e Q
        stacked = np.concatenate([I, Q])
        _, scale = self._scale_to_uint(stacked)

        max_uint = (1 << self.resolution) - 1
        half = max_uint // 2

        def apply_scale(x):
            mapped = half + x * scale
            mapped = np.round(mapped).astype(np.int64)
            mapped = np.clip(mapped, 0, max_uint)
            return mapped.astype(np.uint16)

        Iu = apply_scale(I)
        Qu = apply_scale(Q)

        # interleave: I0,Q0,I1,Q1,...
        iq_u = np.empty(Iu.size * 2, dtype=np.uint16)
        iq_u[0::2] = Iu
        iq_u[1::2] = Qu

        header_prefix = arr_name.upper()
        guard_name = header_prefix + "_H"

        lines = []
        lines.append(f"#ifndef {guard_name}")
        lines.append(f"#define {guard_name}")
        lines.append("")
        lines.append("#include <stdint.h>")
        
        self.write_stream_data_type(lines)
        self.write_stream_meta_data(lines,arr_name,iq_u.size,scale,info)
        
        lines.append(f"static const stream_data_t {arr_name}[] = {{")

        for i in range(0, iq_u.size, elem_per_line):
            chunk = iq_u[i:i + elem_per_line]
            chunk_str = ", ".join(str(int(v)) for v in chunk)
            if i + elem_per_line >= iq_u.size:
                lines.append(f"    {chunk_str}")
            else:
                lines.append(f"    {chunk_str},")

        lines.append("};")
        lines.append("")
        lines.append(f"#endif /* {guard_name} */")
        lines.append("")

        header_text = "\n".join(lines)
        Path(save_path).write_text(header_text)
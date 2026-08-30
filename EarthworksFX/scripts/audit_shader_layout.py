#!/usr/bin/env python3
"""Structured-buffer layout audit: D3D12 vs Vulkan.

For every struct used as a Structured/RWStructuredBuffer element in the active
shader tree, compile the real shaders with DXC's SPIR-V backend (the same
compiler Diligent uses for Vulkan) and diff the member offsets it assigns
against C-tight packing - which is what MSVC writes on the C++ side and what
DXIL uses for structured buffers on D3D12.

Background: Diligent does not pass -fvk-use-dx-layout, so DXC lays SPIR-V
storage buffers out with "vector-relaxed std430". That matches C-tight packing
for most hand-16-byte-grouped structs, but diverges for vec3/vec4 ARRAYS
(std430 array stride is not relaxed: float3 arrays get stride 16) and for
vectors whose tight offset would straddle a 16-byte boundary. A divergent
struct silently reads garbage on Vulkan while rendering fine on D3D12
(sprite_material was exactly this, 2026-08).

Run it after touching any hlsli/hlsl struct that a StructuredBuffer uses:

    python EarthworksFX/scripts/audit_shader_layout.py

Expected output: "0 diverging struct(s)". Any diverging member is a live
Vulkan corruption bug - fix the struct (see the TF_material layout-contract
comment in hlsl/terrain/materials.hlsli), don't ignore it.

Requires dxc.exe with SPIR-V support (Vulkan SDK; the Windows SDK dxc cannot
emit SPIR-V). Set DXC env var to override auto-detection.
"""
import glob as _glob
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HLSL = Path(__file__).resolve().parent.parent / "hlsl"


def find_dxc():
    if os.environ.get("DXC"):
        return os.environ["DXC"]
    hits = sorted(_glob.glob(r"C:\VulkanSDK\*\Bin\dxc.exe"))
    if not hits:
        sys.exit("dxc.exe not found - install the Vulkan SDK or set the DXC env var")
    return hits[-1]  # newest SDK


DXC = find_dxc()
OUT = Path(tempfile.mkdtemp(prefix="ew_layout_audit_"))

# stage -> (profile, entry-name candidates)
STAGES = [
    ("cs_6_5", ["main"]),
    ("vs_6_5", ["vsMain", "VSMain", "vs_main"]),
    ("ps_6_5", ["psMain", "PSMain", "ps_main"]),
    ("gs_6_5", ["gsMain", "GSMain"]),
]


def compile_file(f: Path):
    """Compile every stage whose entry point exists in the file; return spvasm texts."""
    src = f.read_text(errors="replace")
    outs = []
    for profile, entries in STAGES:
        for e in entries:
            if not re.search(rf"^\s*[\w<>]+\s+{e}\s*\(", src, re.MULTILINE):
                continue
            spv = OUT / (f.stem + "." + profile.split("_")[0] + ".spvasm")
            cmd = [DXC, "-spirv", "-fspv-reflect", "-Od", "-T", profile, "-E", e,
                   "-I", str(HLSL), "-I", str(HLSL / "terrain"),
                   "-D", "CALLEDFROMHLSL=1",
                   str(f), "-Fc", str(spv)]
            r = subprocess.run(cmd, capture_output=True, text=True)
            if r.returncode == 0 and spv.exists():
                outs.append(spv.read_text(errors="replace"))
            else:
                print(f"  [FAIL] {f.name} {profile}:{e}: {r.stderr.strip().splitlines()[:2]}",
                      file=sys.stderr)
            break
    return outs


# ---- SPIR-V disasm parsing -------------------------------------------------
def parse_spvasm(text):
    names, member_names, offsets, types, consts, strides = {}, {}, {}, {}, {}, {}
    for line in text.splitlines():
        line = line.strip()
        m = re.match(r'OpName (%[\w.]+) "([^"]+)"', line)
        if m: names[m.group(1)] = m.group(2)
        m = re.match(r'OpMemberName (%[\w.]+) (\d+) "([^"]+)"', line)
        if m: member_names.setdefault(m.group(1), {})[int(m.group(2))] = m.group(3)
        m = re.match(r'OpMemberDecorate (%[\w.]+) (\d+) Offset (\d+)', line)
        if m: offsets.setdefault(m.group(1), {})[int(m.group(2))] = int(m.group(3))
        m = re.match(r'OpDecorate (%[\w.]+) ArrayStride (\d+)', line)
        if m: strides[m.group(1)] = int(m.group(2))
        m = re.match(r'(%[\w.]+) = OpConstant %\w+ (\d+)', line)
        if m: consts[m.group(1)] = int(m.group(2))
        m = re.match(r'(%[\w.]+) = OpType(\w+)(.*)', line)
        if m: types[m.group(1)] = (m.group(2), m.group(3).strip())
    return names, member_names, offsets, types, consts, strides


def tight_size(tid, types, consts):
    """C-tight size of a SPIR-V type (MSVC-with-glm / DXIL structured-buffer packing)."""
    kind, rest = types[tid]
    if kind in ("Int", "Float"):
        return 4
    if kind == "Vector":
        elem, n = rest.split()
        return tight_size(elem, types, consts) * int(n)
    if kind == "Matrix":
        col, n = rest.split()
        return tight_size(col, types, consts) * int(n)
    if kind == "Array":
        elem, cnt = rest.split()
        return tight_size(elem, types, consts) * consts[cnt]
    if kind == "Struct":
        return sum(tight_size(t, types, consts) for t in rest.split())
    raise ValueError(f"unhandled type {kind}")


def audit_struct(sid, member_names, offsets, types, consts):
    """Return ([(member, spirv_off, tight_off) mismatches], tight_total)."""
    kind, rest = types[sid]
    assert kind == "Struct"
    member_tids = rest.split()
    mism, tight_off = [], 0
    for i, tid in enumerate(member_tids):
        so = offsets.get(sid, {}).get(i)
        mname = member_names.get(sid, {}).get(i, f"m{i}")
        if so is not None and so != tight_off:
            mism.append((mname, so, tight_off))
        # resync on the SPIR-V offset so one shift doesn't cascade the report
        base = so if so is not None else tight_off
        tight_off = base + tight_size(tid, types, consts)
    return mism, sum(tight_size(t, types, consts) for t in member_tids)


def main():
    results, strides_seen = {}, {}
    files = [f for f in sorted(HLSL.rglob("*.hlsl"))
             if "legacy" not in str(f) and "StructuredBuffer<" in f.read_text(errors="replace")]
    for f in files:
        for text in compile_file(f):
            names, member_names, offsets, types, consts, strides = parse_spvasm(text)
            for tid, (kind, rest) in types.items():
                if kind != "RuntimeArray":
                    continue
                elem = rest.split()[0]
                if types.get(elem, (None,))[0] != "Struct":
                    continue
                sname = names.get(elem, elem)
                try:
                    mism, total_tight = audit_struct(elem, member_names, offsets, types, consts)
                except ValueError as e:
                    results.setdefault(sname, set()).add(("PARSE-ERROR", str(e), 0))
                    continue
                results.setdefault(sname, set()).update(mism)
                if tid in strides:
                    strides_seen.setdefault(sname, set()).add((strides[tid], total_tight))

    print("\n===== SHADER LAYOUT AUDIT (SPIR-V vs C-tight) =====")
    clean, dirty = [], []
    for sname in sorted(results):
        mism = results[sname]
        stride_bad = {s for s in strides_seen.get(sname, set()) if s[0] != s[1]}
        if not mism and not stride_bad:
            strs = "/".join(str(s[0]) for s in strides_seen.get(sname, set())) or "?"
            clean.append(f"{sname} (stride {strs})")
        else:
            dirty.append(sname)
            print(f"\n*** {sname}: DIVERGES")
            for (mn, so, to) in sorted(mism, key=lambda x: x[1] if isinstance(x[1], int) else 0):
                print(f"    member {mn}: SPIR-V offset {so}, C-tight offset {to}")
            for (s, t) in stride_bad:
                print(f"    element stride: SPIR-V {s}, C-tight {t}")
    print("\n--- clean (SPIR-V == C-tight):")
    for c in clean:
        print("   ", c)
    print(f"\n{len(dirty)} diverging struct(s), {len(clean)} clean")
    return 1 if dirty else 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Regenerate the vendored DSP word array; unnecessary for normal builds."""
import subprocess
import sys
import tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[1] / "third_party/nxdk-audio"
assembler = sys.argv[1] if len(sys.argv) > 1 else "dsp56300-asm"
with tempfile.TemporaryDirectory() as directory:
    output = Path(directory) / "passthrough.out"
    subprocess.run([assembler, "-f", "lod", "-o", str(output),
                    str(root / "passthrough.a56")], check=True)
    words = {}
    for line in output.read_text().splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "P":
            words[int(parts[1], 16)] = int(parts[2], 16)
    if not words or max(words) >= 2048:
        raise SystemExit("DSP program is empty or exceeds its 8 KiB allocation")
    header = "// SPDX-License-Identifier: MIT\n"
    header += "// Derived from Ryzee119 passthrough.a56, assembled with dsp56300-asm 1.0.0.\n"
    header += "static const uint32_t passthrough_words[] = {\n"
    header += "".join(f"    0x{words.get(i, 0):06x}u,\n" for i in range(max(words) + 1))
    header += "};\n"
    (root / "passthrough_words.h").write_text(header)

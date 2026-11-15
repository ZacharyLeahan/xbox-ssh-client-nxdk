# XSSH streaming playback subset

Derived from https://github.com/Ryzee119/nxdk-audio at commit
fc2deca2cc1e434805ac03ca7c2f500b3b028f36, Copyright 2026 Ryzee119,
under the MIT license. Original SPDX notices are retained.

XSSH uses only stereo 16-bit PCM double-buffered streaming. The APU/AC97
initialization and stereo DSP output required by that path are included.
Static-buffer submission, the HRTF dataset, spatial examples, and CMake/Cargo
build integration are excluded. A generated DSP word array is included, so
normal XSSH builds need neither Rust nor a DSP assembler.

Local changes bound PIO/reset waits and ISR list traversal, disable hardware
on unexpected traps, retain DMA allocations after faults, detach CPU
interrupt callbacks during fault cleanup, and prohibit destruction of a
voice before stop is confirmed. Voice creation initializes pitch before
format conversion and fixes routing to the stereo output bins. The streaming completion callback includes
the completed buffer identity. Application callbacks only publish an atomic
completion bit; filling and queuing happen on the MP3 worker.

passthrough_words.h was generated with dsp56300-asm 1.0.0 from the included
passthrough.a56. Its unused HRTF mixing instructions were removed. Regenerate
with tools/build-audio-dsp.py if the assembly changes.

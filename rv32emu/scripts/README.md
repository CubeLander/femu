# rv32emu Scripts

This directory contains **emulator-local helper tools**.

## Current Tools

1. `trace_linux_path.py`
   - Parse `rv32emu` trace logs.
   - Optional symbolization with Linux `System.map`.
   - Print path/hotspot summaries, optional phase report, and optional JSON output.

## Rule

Keep scripts here focused on emulator-internal data formats and workflows.
Cross-project build orchestration belongs in `/workspace/scripts/`.

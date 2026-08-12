# ESP-Brookesia

This repository contains two related ESP-Brookesia examples:

- [`03_esp-brookesia`](../examples/esp-idf/03_esp-brookesia/) — the source-maintained rich UI example retained for compatibility and CI coverage.
- [`99_esp-brookesia`](../examples/esp-idf/99_esp-brookesia/) — the **Brookesia App Platform** baseline for developing and running additional statically bundled Phone Apps on the current board.

For the current 99 architecture, canonical vocabulary, lifecycle boundaries, data flows, constraints, known defects, and undecided product policies, see [99 ESP-Brookesia Architecture](architecture/99-esp-brookesia.md) and the repository [domain glossary](../CONTEXT.md).

## Runtime Notes

Brookesia examples depend on the repository's selected ESP-IDF, LVGL, display, touch, and board-support component versions. Check the example manifests and CI before changing shared UI or hardware dependencies.

Both examples still require board-level verification for display, touch, memory, and other hardware behavior after a successful build. Factory recovery binaries under [`Firmware/`](../Firmware/) are separate from these source examples and CI-generated release firmware; see [Firmware Artifacts](firmware.md).

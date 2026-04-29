# subghz_NEXT.md — Sub-GHz / RollJam / IR Handoff

## Done this session
- **Hizmos repo research** (https://github.com/Hiktron/Hizmos) — extracted: per-band PA tables (315/433/868/915), 12-freq scan list, ISR-based GDO0 pulse capture, OOK protocol-timing constants (PT2262/EV1527 350µs/1050µs, sync 10500µs). Notes in conversation only — not yet ported into `src/main.cpp`.

- **Memory** — `feedback_web_fetch_permissions.md` (no curl prompts).

## In-progress: dual CC1101 + IR bring-up
- **CC1101 #2 hardware**: wired with CS=GPIO 2 + 10kΩ pull-up to 3.3V, GDO0=GPIO 12 + 10kΩ pull-down to GND. **Bus contention** — both CC1101 #1 and #2 read partnum=0x00 version=0x00. Flash also fails: `WARNING: Failed to communicate with the flash chip` because CC1101 GDO0 drives HIGH at boot → GPIO 12 (MTDI strapping) HIGH → ESP32 reads 1.8V flash mode.

- **Princeton/IOCFG0/replay-alignment fixes** in `src/main.cpp` from prior session: present in code, **never flashed/verified on hardware**.

## Next 3 tasks (in order)

3. **Flash main.cpp (Princeton + IOCFG0 + replay align fixes), test 315MHz fan replay** with serial `'6'` key. This was the original critical task — fan never responded because user only tried 433/390 MHz, not the actual fan band 315 MHz.

## Open decisions / blockers
- **Pin budget exhausted on ESP32-WROOM-32D.** Free bidirectional GPIOs all strapping (0/2/12). Proper second-radio support may need PRD §7.2 reconciliation.


## DO NOT next session without reading

- **Do NOT assume Princeton/IOCFG0/replay fixes work** — they're committed but untested on hardware.

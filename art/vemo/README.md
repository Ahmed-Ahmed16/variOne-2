# Vemo Art — VariOne mascot assets & style guide

Single home for all **Vemo** (the VariOne panda-bot mascot) artwork. Link to this folder
from anywhere that needs the art. Keep it consistent — this file is the source of truth for
how Vemo looks and how device frames are produced.

## Folder map

```
art/vemo/
├── README.md          ← this style guide
├── masters/           ← high-res source art (vector preferred)
│   ├── vemo_bgless.svg     (master vector, transparent bg)
│   └── vemo_bgless.png     (master raster)
├── reference/         ← mood/pose/brand reference sheets (design intent)
│   └── vemo_reference_sheet_v2_logo_mark.png
├── logo/              ← VariOne wordmark / logo lockups
│   ├── VARIONE_logo-removebg-preview.png
│   └── VARIONE logo.jpeg
└── device/            ← copies of the on-device frames (canonical = sd_files/, see below)
    ├── vemo_head.png  vemo_scan.png  vemo_success.png  vemo_error.png  boot.png
```

## Drop the chat-pasted sheets here (use these exact names)

Owner is adding the sheets shared in chat. Save them as:

- `reference/vemo_logo_marks.png` — the face/logo-mark variations sheet (circle / hexagon /
  pin / shield / app-icon framings).
- `reference/vemo_mascot_system.png` — Expressions / Poses / Angles sheet.
- `reference/vemo_states_emotions.png` — Sizes / Modes / Key-States / Emotions sheet.
- `reference/vemo_brand_direction.png` — "Direction 1 — Friendly Signal Explorer"
  (logo + palette + poster/social styles).

Put any future raw mascot art in `masters/` (vector if possible), reference/mood sheets in
`reference/`.

## Brand palette (locked)

| Name | Hex | Use |
|------|-----|-----|
| Deep Navy | `#0B2E63` | body, outlines, dark fills |
| Signal Cyan | `#29C7F6` | accents, the "V" mark, active glow |
| Bright Sky | `#67E9FF` | highlights, visor glow |
| Soft White | `#F7FCFF` | face, light fills |
| Mist Blue | `#D9F3FF` | subtle backgrounds |

Device UI runs a **minimal blue/black** theme (cyan-on-black). On-device renders read as
white/cyan line-art on black — design frames to survive that.

## Who Vemo is (keep consistent)

- A friendly **panda-bot**: rounded white body, blue ears/limbs, dark **visor face** with a
  cyan expression (eyes/mouth as glowing cyan shapes), the cyan **"V" / V-One mark** on the
  chest.
- 10 canonical moods: idle, happy, thinking, sad, angry, sleeping, success, fail, working,
  waving. Map to device states (see plan Batch 9):
  scan→**working**, connect/read OK→**success/waving**, failure→**sad/fail**,
  idle timeout→**sleeping (Zzz)**, boot→**waving**.

## Consistency rules

- Only the palette above. No off-brand colors, gradients only where the masters use them.
- Keep the visor + cyan "V" chest mark in every full-body/face pose — that's the identity.
- Outlines = Deep Navy; expression = Signal/Sky cyan; face = Soft White.
- Don't restyle the character per screen; expressions change, the design language doesn't.

## Device frames (firmware)

- **Canonical on-device art lives in `sd_files/themes/VariOne_Vemo/`** (and mirror in
  `data/themes/VariOne_Vemo/`). The copies in `device/` here are for reference only — edit
  the `sd_files` ones for the firmware, then refresh the copies here.
- **Hard limit: every theme PNG ≤ 160 px wide** or the device PNG decoder overflows and
  crashes. Head/mood frames target the small head box (~64–80 px square).
- Naming on device stays **Vemo** (one e) — `vemo_head.png`, `vemo_scan.png`,
  `vemo_success.png`, `vemo_error.png`, `boot.png`.
- For the animation work (plan Batch 9): render small per-mood head frames from the masters
  (e.g. `vemo_head.png` + `vemo_head_blink.png`, 2-frame success/error), 1-bit-friendly,
  palette above. The `jiggly-avalanche` worktree has helper tools
  (`tools/crop_poses.py`, `tools/png_to_rgb565.py`) if a sprite path is ever chosen.

## Notes

- `vemo_head.png` currently on device is a **placeholder** — replace with a frame rendered
  from these masters.
- The big banners / print assets stay in `UImarketing/print_ready/` (not duplicated here).

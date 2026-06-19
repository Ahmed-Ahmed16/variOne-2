# VariOne — Documentation Handoff (2026-06-19)

Scope: graduation-paper reconciliation. No firmware touched this session.
Separate from the firmware session handoff (`HANDOFF-2026-06-19.md`).

## State
- Branch **finale-fixes**, tip **58fd038**, **SYNCED** with origin at session start
  (`git ls-remote origin finale-fixes` == HEAD). No firmware commits this session.
- Build: **not built** — documentation-only work, no firmware changed.
- HW-confirmed: **none this session** (doc work).
- Working tree: all doc work is in **untracked `Documentation/`** (by design) — not
  committed; lives in the working tree / Overleaf. Only handoffs are committed.

## What shipped (no git commits — files in untracked `Documentation/finale/`)
| file | what |
|------|------|
| `Documentation/finale/vari_final.tex` | **NEW canonical paper** — her tone/depth + his Times/booktabs style + repo-accurate. 63 pages, compiles green via `latexmk -xelatex` (0 missing images, 0 undefined refs, 1 trivial 1.7mm overfull). |
| `Documentation/VariOne_documentation_FINAL.pdf` | compiled output (refreshed each pass). |
| `Documentation/finale/CHANGES_his_vs_final.diff`, `CHANGES_hers_vs_final.diff` | full review diffs. |

Work done:
- Merge base = his preamble/style + her offensive abstract/body + his repo-accurate AI-debrief Ch.7.
- **Accuracy vs repo:** OLED→ST7735S TFT; 4→6-button (Listing 5.1 now real `varione-s3.ini` flags); `/captures/*`→`/Vari*` paths; fabricated `PIN_*` macros + `VariOneHardware::initSPI()` → real `_setup_gpio()`; Vemo "10 moods"→real `Scan/Idle/Sleep/Success/Error`; deauth "3 modes"→Target Atks + Deauth Flood; AI debrief→Gemini-2.0-flash + LAN/Ollama, real file paths.
- **Restored her depth** (Codex had trimmed): G1–G11, R1–R3, FR1–FR8, UC1–UC10, dual-core listing, reality-checks, ERD, menu-nav figure, 21 technical refs.
- **Dr/ledger reqs:** Gantt figure→table (now in List of Tables; fixed duplicate image — Part 1 `gantt_1.jpeg`, Part 2 `gantt_2.jpeg`); added S3 pin-map table (`tab:s3_pinmap`, incl. nRF24 SPI3 ghost pins 1/21); added Physical Implementation block diagram (`fig:hardware`); softened `<100ms` latency; LoF/LoT/References added to TOC.
- **BOM** rebuilt from real invoice #0-1709 with the actual swaps (WROOM→S3 550, OLED→TFT 350, IR-rx→IR-TRX ~50, +CC1101 840, +nRF24 250, no battery): **total ~2,545 (rounded ~2,550, under USD 100)**.
- Reviewed Codex's `DOCUMENTATION_HANDOFF.md` + 3 ledgers (`DOCUMENTATION_REVIEW_QUESTIONS.md`, `BRANCH_ALIGNMENT_NOTES.md`, `OVERLEAF_SYNC_CHECKLIST.md`).

## Key invariants — DO NOT regress (documentation)
- **`vari_final.tex` is the canonical paper.** NOT `vari.tex` (his/Codex) and NOT `lasttTTESt.tex`.
- **`lasttTTESt.tex` on disk is the friend's PRE-FIX version** — still carries OLED/4-button/`/captures`/10-mood/fabricated listings. Do NOT merge it wholesale; only 3 blocks were ported (S3 pin-map, shared-bus prose, Physical-Impl diagram).
- **Tone = offensive/red-team.** Do NOT reintroduce Codex's legal/authorization/passive/"safety" language.
- **Repo-accurate facts to keep:** ST7735S TFT-only display, 6-button map, Gemini-2.0-flash + LAN AI-debrief, `/Vari*` storage paths.
- **Gantt = tables** (Dr decision) in List of Tables, never List of Figures.
- Watermark wrapped in `\ifdefined\transparent` (renders in Overleaf, no-op locally).
- Compile with **xelatex from the `Documentation/` dir** so `finale/` image paths resolve.

## Open / not done
- **IR-TRX = 50 EGP is an ESTIMATE** (user hasn't given the real price) — only soft BOM number left; everything else invoice/owner-confirmed.
- **BOM owner-decision conflict:** ledger #3/#17 said keep breadboard ~2,000; user overrode to invoice-based S3 ~2,545/2,550 this session.
- Doc not committed (`Documentation/` untracked by design) — lives only in working tree / Overleaf.
- PAN policy documented (full in lab, last-4 outside, saving optional); repo `emv_reader.cpp` has no mask helper (code-side, separate from doc).
- `vari.tex` (his) and `lasttTTESt.tex` (her) left untouched beside the final.

## Next session
TBD — paper is near-final pending the real IR-TRX price and user/Dr final review.
Entry point: `Documentation/finale/vari_final.tex`. Relevant memory: [[doc-reconcile-status]].

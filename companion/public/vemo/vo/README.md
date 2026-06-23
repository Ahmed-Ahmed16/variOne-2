# Vemo voice-over clips (Attack Control Room)

Drop pre-rendered Vemo VO here, one MP3 per act. The Theater plays the matching
clip when the presenter advances to that act. **Clips are opt-in by existence** —
if a file is missing, that act just runs silent. The show never depends on audio.
Mute/unmute live with the **V** key.

## Expected filenames

| File | Act | Vemo's line (see `lib/theater/vemoLines.ts`) |
|------|-----|----------------------------------------------|
| `recon.mp3`   | Recon opener   | "…I already know you. Your phone never stops talking." |
| `ble.mp3`     | BLE blitz      | "Watch your hands — I'm about to make every phone here buzz." |
| `deauth.mp3`  | Targeted deauth| "You all joined my old router earlier. Check your WiFi again." |
| `clone.mp3`   | Clone & rejoin | "…they rejoined — me. Count only here, I promise." |
| `badusb.mp3`  | BadUSB         | "This isn't a USB stick. It's a keyboard…" |
| `debrief.mp3` | Debrief        | "Four attacks, under five minutes — and every one has a defence." |

## Recording notes

- Keep each clip short (~4–8 s); it plays once on act entry.
- Match the lines in `vemoLines.ts` (or improvise in character — mischievous
  analyst who teaches). The `peak`/`done` lines there are extra beats you can
  fold into one clip.
- Normalize loudness so no clip is much louder than the room mic.
- MP3, mono is fine. Filenames are lowercase, exactly as above.

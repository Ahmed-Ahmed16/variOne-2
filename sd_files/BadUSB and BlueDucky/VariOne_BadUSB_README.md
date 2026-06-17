# VariOne BadUSB — awareness demo scripts

BadUSB = USB HID keystroke injection. VariOne enumerates as a **USB keyboard** and types a
Ducky Script on its own. These two scripts are deliberately **harmless** (graduation
awareness demo, not malware): they only open Notepad / a URL and type text. No files are
changed, nothing is downloaded, no commands run.

## Files
- `VariOne_demo_hello.txt` — opens Notepad and types an awareness message.
- `VariOne_demo_url.txt` — opens the Run dialog to an awareness URL.

Both are Windows demos (`GUI r` = Win+R). For Linux/macOS, swap the launcher line
(e.g. macOS: `GUI SPACE` → Spotlight). Supported Ducky commands on this device:
`REM, DELAY, DEFAULT_DELAY, STRING, STRINGLN, ENTER, GUI/WINDOWS, CTRL, ALT, SHIFT, REPEAT`.

## How to run (device)
1. Main menu → **Others → BadUSB & HID → BadUSB**.
2. Pick the filesystem: **SD Card** (these files ship to the card via `sd_files/`) or
   **LittleFS**.
3. Browse to `BadUSB and BlueDucky/` and choose `VariOne_demo_hello.txt`.
4. Plug VariOne into the target PC's USB port; press **OK** to start. **BACK** cancels.

## Demo script (what to say)
> "This looks like an ordinary USB stick. Watch — I plug it in and don't touch the
> keyboard." → run `VariOne_demo_hello.txt` → it auto-types the awareness message in
> Notepad. **Lesson: a USB device can pretend to be a keyboard; never plug in or trust an
> unknown USB device.**

## Verify checklist (owner, on HW)
- [ ] (a) PC enumerates VariOne as a USB keyboard (Windows Device Manager → Keyboards, or
      `lsusb` on Linux).
- [ ] (b) `VariOne_demo_hello.txt` opens Notepad and types the exact message.
- [ ] (c) `VariOne_demo_url.txt` opens the URL in the Run dialog / browser.
- [ ] (d) **BACK** cancels mid-script (typing stops promptly).
- [ ] (e) Re-running a script works without re-plugging the device.

If keystrokes are missing/garbled, raise the per-key delay (BadUSB config) or add
`DEFAULT_DELAY 50` at the top of the script.

# Vemo Mascot Consistency Guide

## Purpose

This file is the visual source of truth for **Vemo**, the VariOne mascot. Give this file to designers, video editors, image-generation agents, and future contributors before they create any new mascot art.

Goal:

> Vemo should look like the same character in every image, pose, banner, video, sticker, and UI mockup.

Reference image:

- `UI-MARKETING/vemo_reference_sheet_v1.png`
- Editable reusable SVG mascot asset:
  - `UI-MARKETING/vemo_character_master.svg`
  - `UI-MARKETING/vemo_character_master_preview.png`

This first sheet is a style direction, not a perfect final lock. Future versions should improve consistency while preserving the rules below.

## Character Identity

Name: **Vemo**

Role:

- VariOne mascot
- Cute helper
- Student learning companion
- Friendly cyber-awareness guide
- Emotional reaction character for demos and social content

Personality:

- Curious
- Helpful
- Smart but not arrogant
- Funny but not childish
- Excited by discoveries
- Non-threatening
- Student-lab friendly

Core feeling:

> A cute little tech companion that makes cybersecurity feel friendly and understandable.

## Brand Relationship

Vemo belongs to **VariOne**.

The chest mark must use the **VariOne logo mark only**, not the full wordmark and not the motto.

Logo reference:

- Use the large cyan stylized **V/O symbol** from the provided VariOne logo.
- Do **not** include the small text/motto under the logo.
- Do **not** write "VARIETY IN ONE" on the mascot body.
- Do **not** put readable text on the mascot.

Chest logo rule:

- Place the cyan VariOne symbol centered on Vemo's chest.
- Simplify it if needed so it reads clearly at small sizes.
- Keep it as a clean icon, not a detailed wordmark.
- It should look printed or embedded into the chest panel.
- It should never replace the face or visor.

## Canonical Shape

Vemo's silhouette must stay consistent:

- Small, chubby, compact body
- Large rounded head, slightly wider than the body
- Rounded bear/cat-like ears built into the head shape
- Short rounded arms
- Oversized rounded gloves
- Short rounded legs
- Oversized rounded boots
- Soft toy-like proportions
- No sharp aggressive armor
- No realistic fur
- No human hair
- No visible mouth unless the expression needs a tiny simple smile inside the visor

Recommended proportions:

- Head: about 45-50% of total height
- Body: about 30-35% of total height
- Legs/boots: about 15-20% of total height
- Arms: short enough to feel cute, long enough to gesture clearly
- Gloves: large and expressive
- Boots: large, rounded, stable

## Face And Visor

The face is a dark navy rounded visor.

Visor rules:

- Large rounded horizontal visor
- Dark navy or deep blue fill
- Glossy highlight allowed
- Cyan glowing eyes
- Eyes are the main expression system
- Keep the visor shape consistent across poses

Eye rules:

- Cyan glow
- Simple expressive shapes
- No realistic pupils
- No scary red eyes
- No angry villain face
- Expressions should be readable even at small social-media size

Common expressions:

- Happy: crescent eyes or bright oval eyes
- Thinking: one eye slightly squinted, curious angle
- Working: focused glowing oval eyes
- Shocked: wide round glowing eyes
- Sad: droopy cyan eyes
- Angry: narrowed cyan eyes, still cute
- Sleepy: closed curved cyan eyes
- Success: happy crescent eyes, confident pose

## Color Palette

Primary colors:

- White shell: clean glossy white
- Deep navy outline: used for strong readable sticker-style edges
- Cyan glow: eyes, UI accents, logo mark
- Medium blue panels: ears, gloves, boots, side accents
- Pale blue shadows: soft shading on white shell

Approximate palette:

- Deep navy: `#0B2E63`
- Visor navy: `#102B5C`
- Bright cyan: `#29C7F6`
- Soft cyan glow: `#67E9FF`
- Medium blue: `#238DDB`
- Pale blue shadow: `#D9F3FF`
- White shell: `#F7FCFF`

Accent colors:

- Green check for success
- Red X for failure
- Yellow light bulb for ideas

Use accents sparingly. Vemo itself should remain blue/white/cyan.

## Line And Rendering Style

Style:

- Anime tech mascot
- Sticker-like polish
- Thick clean navy outline
- Soft cel shading
- Glossy highlights
- Rounded forms
- Crisp edges
- Bright, friendly, educational

Avoid:

- Photorealism
- Realistic fur
- Dark horror cyber style
- Dirty grunge texture
- Overly complex mechanical parts
- Thin fragile outlines
- Heavy black/red villain palette
- Weapon-like props

## Props

Allowed props:

- Small wrench
- Tiny laptop
- Signal waves
- Light bulb
- Shield icon
- Gear icon
- Check mark
- Question mark
- Small code window
- Small remote/card/Wi-Fi icons

Avoid props:

- Weapons
- Locks being broken
- Stolen cards
- Money/cash theft visuals
- Hooded hacker imagery
- Skulls
- Police/crime imagery

## Mascot Moods

Vemo should have a standard pose set:

1. **Neutral / Idle**
   - Standing upright
   - Calm oval eyes
   - Arms relaxed

2. **Happy / Waving**
   - One hand waving
   - Crescent happy eyes
   - Friendly intro/outro pose

3. **Thinking**
   - One hand on chin or side of head
   - Question marks or light bulb allowed
   - Curious eyes

4. **Working**
   - Holding wrench or looking at small code window
   - Focused eyes
   - Used for build/dev/progress visuals

5. **Shocked**
   - Hands near face
   - Wide cyan eyes
   - Used for surprising security facts

6. **Success**
   - Thumbs up or check mark
   - Happy eyes
   - Used after positive demo result

7. **Fail / Sad**
   - Droopy posture
   - Sad eyes
   - Used after failed scan/read/demo

8. **Angry / Alert**
   - Narrow eyes
   - Small steam or warning accent allowed
   - Still cute, not evil

9. **Sleeping**
   - Curled or sitting pose
   - Closed eyes
   - ZZZ bubble allowed

## Logo Integration Rule

The old placeholder chest square must be removed.

Replace it with:

> The simplified VariOne cyan V/O logo mark, without any text or motto.

The mark should:

- Be centered on the chest
- Fit inside the torso panel
- Stay cyan/blue
- Be readable at small size
- Use the same outline language as the mascot
- Never include the "VARIETY IN ONE" text

If the image model cannot draw the exact mark, use a simplified rounded cyan symbol that clearly suggests the attached VariOne V/O logo, then a designer can clean it manually later.

## Image Generation Master Prompt

Use this prompt for new Vemo assets:

```text
Create Vemo, the official VariOne mascot, as a consistent cute anime-tech robot companion. Vemo has a chubby compact body, a large rounded head, small rounded bear/cat-like ears, a dark navy rounded visor face, glowing cyan expressive eyes, a glossy white shell, medium-blue gloves and boots, cyan-blue side panels, thick clean navy outlines, soft cel shading, and polished sticker-art highlights.

Place the simplified VariOne logo mark only on the center of Vemo's chest: a cyan stylized V/O symbol based on the provided VariOne logo, with no motto text and no readable words. Do not use the old square chest emblem.

Keep Vemo friendly, student-lab themed, approachable, and non-threatening. The character should look like the same mascot in every pose with identical proportions, same head shape, same ears, same visor, same gloves, same boots, same chest logo placement, and same blue-white-cyan palette.

Style: polished anime mascot sticker art, crisp edges, thick navy outline, glossy white body, cyan eye glow, bright educational cyber-awareness feeling. Avoid photorealism, realistic fur, scary hacker imagery, weapons, dark villain styling, skulls, and any readable text.
```

## Reference Sheet Prompt

Use this for a full pose sheet:

```text
Create a clean character reference sheet for Vemo, the official VariOne mascot. Show the exact same mascot design repeated across the sheet with identical proportions, colors, visor, ears, gloves, boots, and chest logo.

Vemo is a cute compact anime-tech robot companion with a glossy white shell, medium-blue gloves and boots, cyan-blue side panels, rounded bear/cat-like ears, a dark navy rounded visor, and glowing cyan expressive eyes. On the center of the chest, place the simplified VariOne cyan V/O logo mark only, with no motto text and no readable words.

Sheet layout: one large hero three-quarter pose in the center, plus smaller poses around it: front neutral, side view, back view, happy/waving, thinking, working with small wrench, shocked, success/thumbs-up, sad/fail, and sleeping. Use a clean white or very light cool-gray studio background. No transparency checkerboard. No labels. No watermark.

Style: polished anime mascot sticker art, thick navy outlines, soft cel shading, glossy highlights, crisp edges, friendly educational tech feel.
```

## Sticker Prompt

Use this for individual sticker poses:

```text
Create one full-body sticker illustration of Vemo, the official VariOne mascot, in a [POSE/MOOD] pose. Keep the exact canonical design: chubby compact white robot body, rounded head with bear/cat-like ears, dark navy rounded visor, glowing cyan expressive eyes, blue gloves, blue boots, thick navy outline, soft cel shading, and the simplified cyan VariOne V/O logo mark centered on the chest with no text.

Mood: [MOOD].
Prop if needed: [PROP].
Background: plain white or flat chroma-key if cutout is needed.
Avoid readable text, scary hacker imagery, weapons, skulls, photorealism, and redesigning the mascot.
```

## Designer Cleanup Notes

AI image generation may not reproduce the exact VariOne logo perfectly. For final production assets:

- Generate Vemo with an approximate cyan V/O chest mark.
- Manually replace the chest mark with the real vector logo mark.
- Keep the logo mark without text.
- Save a clean vector or high-resolution PNG mascot pack after cleanup.

Recommended production files:

- `vemo_reference_sheet_final.png`
- `vemo_happy.png`
- `vemo_thinking.png`
- `vemo_working.png`
- `vemo_shocked.png`
- `vemo_success.png`
- `vemo_fail.png`
- `vemo_sleeping.png`
- `vemo_logo_mark.svg`

## Current Open Decisions

- Final mascot name: currently recommended as **Vemo**.
- Final logo vector: needed for exact chest placement.
- Final pose pack: should be generated/cleaned after the chest logo direction is approved.

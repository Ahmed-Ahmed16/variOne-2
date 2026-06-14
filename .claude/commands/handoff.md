---
description: Generate a standard end-of-session handoff note, write it to handoffs/, and commit it
argument-hint: "[optional: next-feature focus]"
---

Produce a **VariOne session handoff note** for the work done this session, in the
exact standard format below. Fill every section from the live session context —
do not fabricate; if a section has nothing, write "none this session".

## Step 1 — gather facts (run these, use real output)
- Branch + remote sync (trust `ls-remote`, not `git branch -r`):
  - `git rev-parse --abbrev-ref HEAD`
  - `git rev-parse HEAD`
  - `git ls-remote origin $(git rev-parse --abbrev-ref HEAD)`  ← compare to HEAD; state SYNCED or AHEAD/unpushed
- This session's commits: `git log --oneline -15` (include only the commits made this session).
- Build status: state whether `pio run -e varione-s3` was run this session and its result (pass/fail, flash/RAM %). Do NOT claim a build passed unless it was actually run — say "not built this session" otherwise.
- Working tree: `git status -s` (note any uncommitted changes).

## Step 2 — write the note
Write to `handoffs/HANDOFF-<YYYY-MM-DD>.md` (use today's date; if the file exists,
append a new `## Session <N>` block, don't overwrite). Use this structure:

```
# VariOne S3 — Session Handoff (<date>)

## State
- Branch <name>, sync status (tip <sha>, SYNCED/unpushed vs origin).
- Build: <result or "not built this session">.
- HW-confirmed: <what the user verified on hardware this session>.

## What shipped (commits oldest->newest)
<table: commit | what | HW status>

## Key invariants — DO NOT regress
<bullets: the load-bearing decisions a future session must not break, with file pointers>

## Open / not done
<bullets: known gaps, things awaiting HW test, deferred items>

## Next session
<focus from $ARGUMENTS if given, else "TBD">. Entry points: <files/dirs>. Relevant memory: <[[slugs]]>.
```

## Step 3 — persist
- Save/refresh any durable facts as memory files (per the memory rules) — especially
  new invariants and the next-session plan. Update `MEMORY.md` pointers.
- Stage and commit ONLY the handoff file (and memory is outside the repo, skip it):
  `git add handoffs/ && git commit` with message `docs(handoff): session <date>`.
  Do not push unless the user asks.

## Rules
- Match the project's CLAUDE.md and the "evidence before assertions" rule: every
  "fixed/passing" claim must trace to a command output or a user HW confirmation
  quoted in this session. Flag anything unverified as such.
- Keep it scannable. No filler.

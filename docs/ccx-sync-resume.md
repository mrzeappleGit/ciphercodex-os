# RESUME HERE — X4 WebDAV sync, paused 2026-07-13

Handoff for the next Claude session. Work is mid-execution, cleanly resumable.

## What this is

Executing the 8-task plan `docs/superpowers/plans/2026-07-13-x4-webdav-sync.md` **in the
cipherCodex repo** (`G:\nextcloud\projects\cipherCodex`) via
**superpowers:subagent-driven-development** (fresh implementer per task → task review →
ledger). It ports the frozen phase3b WebDAV sync (live on Android v0.5.0 + rM2) to this
firmware. Spec: cipherCodex `docs/superpowers/specs/2026-07-12-webdav-sync-android-x4-design.md`
(X slices). Wire contract: `remarkable2-os/docs/phase3b-contracts.md` — **`format` is an
int on the wire: 1 = epub, 0 = pdf** (E2E-proven; overrides the spec's string comparison).

⚠️ Plan-file gotcha: the plan is committed on the cipherCodex repo's
`handwriting-recognition` branch (commit `8627e2a`) because a parallel rM2 session had
switched that shared checkout's branch. The file is present in the working tree either
way; don't be surprised by `git log main` not showing it.

## Exact state

- This repo (x4-os): branch `ccx-webdav-sync` (off `develop` at `00213441`).
- Ledger (authoritative): `.superpowers/sdd/progress.md` — **Task 1 complete**
  (`b397feb5`, CcxMerge.h LWW core, 102/102 host tests, review clean). Tasks 2–8 pending.
- Task 2's brief is already extracted: `.superpowers/sdd/task-2-brief.md`.
- Recorded review debt for the final whole-branch review: `foldProgress` unbounded when
  callers skip `setLocalDigestFilter` (plan-mandated).

## How to resume

1. In x4-os: `git checkout ccx-webdav-sync`, read `.superpowers/sdd/progress.md`.
2. Invoke **superpowers:subagent-driven-development** for the plan above, resuming at the
   first task not in the ledger (Task 2). Extract briefs with the skill's
   `scripts/task-brief <plan-path> N` (run from the x4-os root so artifacts land in this
   repo's `.superpowers/sdd/`); build review diffs with `scripts/review-package BASE HEAD`.
3. Models decided: Task 2 haiku (complete code in brief), Tasks 3–7 sonnet, reviewers
   sonnet, final whole-branch review on the most capable model.
4. Environment for implementers:
   - Firmware build: `pio run` from the x4-os root (pio 6.1.19 on PATH).
   - Host tests (GoogleTest) — this Windows box needs the custom toolchain (also in Claude
     memory `x4os-host-test-toolchain`): PowerShell, from x4-os root:
     ```powershell
     $env:PATH = "C:\Users\mrzea\toolchains\mingw64\bin;C:\Users\mrzea\.platformio\penv\Scripts;$env:PATH"
     cmake -S test -B build/test -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
     cmake --build build/test
     ctest --test-dir build/test --output-on-failure -j
     ```
     (MSVC can't build the suite — `__attribute__((packed))` in EpdFontData.h; WSL is broken.)
   - Repo rules bind all code: `.skills/SKILL.md` + `.claude/skills/` (HAL `Storage`/`HalFile`
     only, `makeUniqueNoThrow`/`new (std::nothrow)`, 380KB ceiling, `tr(STR_*)`, `LOG_*`).
5. Task 8 needs the owner: X4 on USB for `pio run -t upload`; the `ccx` WebDAV app
   password (ask for it — never extract it yourself; the auto-mode classifier blocks that);
   QA runs against the REAL `https://kosync.cph.gg/ccx/` base path — ADDITIVE ONLY, never
   delete anything under `books/` or `state/` except the X4's own state file.
6. After Task 8: final whole-branch review (`review-package $(git merge-base develop HEAD) HEAD`),
   one fix wave if findings, then superpowers:finishing-a-development-branch — merge to
   `develop`, version bump + tag per the repo's release ritual (mirror how v0.2.13 shipped).

## Session-sharing warnings

- The **cipherCodex** checkout is shared with a parallel rM2 session (it commits/switches
  branches there). Never switch its branches while it has uncommitted work — ship via
  `git fetch . <branch>:main` fast-forward if needed (that's how v0.6.0 went out).
- This **x4-os** checkout is NOT shared — normal git use is safe here.

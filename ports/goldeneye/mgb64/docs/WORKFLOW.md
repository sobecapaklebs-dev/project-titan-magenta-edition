# Development Workflow — single source of truth

This repository has **one** source of truth: the public repo
[`akratch/mgb64`](https://github.com/akratch/mgb64). All development happens here,
in the open. There is no private "staging" fork, and there is no second `main`.

## Why this doc exists

For a while the project carried two disjoint repos:

- a **private** `mgb64-prepublic-*` staging repo (the original full-history dev tree), and
- the **public** `akratch/mgb64` repo (a curated snapshot that the releases were cut from).

The two shared **no common git ancestor**, so nothing could flow between them except
manual cherry-picks. In practice that meant fixes landed on one side and were silently
missing from the other (e.g. the multi-ammo-crate collect fix shipped on public but never
reached private, so local builds cut from private re-exhibited a bug that was "already
fixed"). The private tree was also perpetually *behind* public on shipped code while
carrying ~11k lines of history and curated-out artifacts nobody wanted.

We collapsed to a single lineage: **public is the source of truth**; the private repo was
archived read-only. This document is the guardrail that keeps us from re-introducing a
second lineage.

## The rules

1. **One remote, one `main`.** `origin` points at `akratch/mgb64`. Local `main` tracks
   `origin/main`. Never add a second long-lived "staging" remote that develops in parallel.
2. **Branch → PR → merge.** Never commit directly to `main`. Cut a topic branch
   (`fix/…`, `feat/…`, `docs/…`), push it, open a PR, and merge it into `main`. This is the
   "develop in the open" model the v0.3.x releases already used.
3. **Install the hooks — they are the structural gate.** Run
   `scripts/install_git_hooks.sh` once per checkout (sets `core.hooksPath
   .githooks`; verify with `git config core.hooksPath`). Two tracked hooks then
   run automatically:
   - **pre-commit** rejects ROM/ROM-derived data in tracked files (no ROM bytes,
     extracted assets, or captured traces/screenshots — see `.gitignore` and
     `docs/RENDERING_REGRESSION_NOTES.md`, "must stay local").
   - **pre-push** additionally, for **any push to the public remote**
     (`akratch/mgb64` / `origin`) — *including a routine `git push -u origin
     fix/whatever` topic branch* — scans the exact commits being pushed (diff
     content **and** commit messages) for the never-leak classes (private
     absolute paths, personal email, credentials, proprietary SDK notice) and
     **refuses** on a hit (`scripts/ci/scan_leak_classes.sh`). This is the
     structural control that closes the day-to-day PR lane: leak-class text
     cannot reach the public repo via a topic-branch push + server-side merge.
     The accepted-public fidelity trees and AI-authorship trailers are **not**
     blocked. The hook also nudges direct `main`/tag pushes through the guarded
     entrypoint below (override for a deliberate manual push:
     `MGB64_ALLOW_DIRECT_PUBLIC_PUSH=1`). A contributor who has not installed the
     hook loses this local backstop — so installing it is not optional.
     **Backstop honesty:** the always-on control is *client-side only* (a
     `--no-verify` push or a clone that skipped the install evades it). CI's
     `check_public_history_text.sh` is currently `workflow_dispatch`-only
     (manual) and scans diff content, not commit messages — so it is a manual,
     partial net, **not** an automatic server-side one. Making it a true net
     (a `pull_request` trigger) is an owner policy call that conflicts with the
     repo's deliberate hosted-runners-disabled posture — see the open decision
     in `docs/fidelity/ESCALATIONS.md`.
4. **Releases are tags on `main`, published only through the guarded path.**
   `vX.Y.Z` tags live on `main` only; cut releases from `main`, never from a fork.
   Routine `main` updates and release tags go through the one guarded entrypoint
   **`scripts/publish_public.sh`** (which the pre-push hook nudges you toward),
   never a raw `git push origin main`. That script refuses a dirty tree, runs the
   release-ready + public-history-text guards **and the same ranged leak-class
   scan the hook runs**, requires a strict green **full-coverage** verify report
   for the commit, and **never force-pushes**. It does not rewrite history and
   does not filter content: the already-public history is accepted as-is and the
   boundary is forward-only (owner ruling 2026-07-11,
   `docs/fidelity/ESCALATIONS.md`, resolved C1/D1). A **release** push
   additionally requires, before the tag:
   - a **strict verify green** report for the release commit
     (`GE007_VERIFY_STRICT=1 tools/fidelity/verify_all.sh`);
   - **owner gameplay verification on macOS AND Windows**, attested to the script
     via `--confirm-gameplay "macos=<initials/date>,windows=<initials/date>"`;
   - the **PortMaster/GLES lane green** (`tools/portmaster_build_check.sh`, or the
     release CI "PortMaster GLES compile check" job);
   - macOS ships **unsigned** (Apple signing is deferred) — the release notes tell
     users to right-click → Open on first launch.

   See `docs/RELEASING.md` for the operational checklist and the internal/external
   boundary doctrine.
5. **Keep `main` releasable.** Merge only branches that build and pass the smoke/ctest
   gates. Land focused PRs; don't stack unrelated work on one branch (the local `main`
   briefly accumulated eight unrelated fixes before this cleanup — that's the anti-pattern).
6. **`export-ignore` filters archives, not this repo.** `.gitattributes` marks
   `docs/design/**`, `docs/fidelity/**`, `tools/fidelity/**`, and `baselines/tapes/**`
   (plus a few named path-gap docs) `export-ignore` because they're internal-only
   working material. That attribute keeps those paths out of `git archive`, GitHub's
   "Download ZIP", and a from-scratch `scripts/create_public_launch_repo.sh` /
   `scripts/prepare_public_launch_bundle.sh` re-launch tree — it does **not** hide them
   from a normal `git clone`/browse of `akratch/mgb64`, because under rule 1 that repo
   *is* the source of truth and every merged commit's full tree is visible there.
   `export-ignore` is a backstop for the archive/re-launch path, not a way to keep a
   path off `main` while still merging it there. If something must never be visible
   in a clone of this repo, it cannot go through the normal branch → PR → merge flow
   at all — that's a call for the maintainer, not something this workflow enforces.

## Day-to-day

```sh
scripts/install_git_hooks.sh                 # once per checkout (enables the hooks)
git switch main && git pull                 # main == origin/main == akratch/mgb64
git switch -c fix/whatever                   # topic branch
# … edit, build (cmake --build build --target ge007), test …
git commit                                   # pre-commit: ROM/asset contamination guard
git push -u origin fix/whatever              # pre-push: leak-class scan of these commits
gh pr create --base main                     # open the PR
# review, then squash-or-merge into main; delete the topic branch
```

This `git push -u origin fix/whatever` lane is **safe by hook**: with the hooks
installed (rule 3) the pre-push leak-class scan runs on exactly these commits
before they can reach the public remote, so a private path / personal email /
credential in the branch is refused at the git layer — no `publish_public.sh`
discipline required for topic branches. (Routine `main` and release-tag pushes
still go through `scripts/publish_public.sh`, which the hook nudges you toward.)

## The archived private repo

The old private `mgb64-prepublic-*` repo is **archived (read-only)** for historical
reference only. It is NOT a development target. If you ever need something from it, cherry-
pick the specific commit onto a public topic branch and run it through the normal PR + guard
path — never merge its history back in (the histories are disjoint by design and a merge
would drag the curated-out artifacts back into public).

## If you find code "missing" from public

That should no longer happen, because there is only one lineage. If some old private-only
change is ever wanted, treat it like any external patch: extract just that change, apply it
on a topic branch off `main`, verify it's contamination-clean and builds, and PR it.

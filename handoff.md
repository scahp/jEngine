# Handoff

## Scope
- Target: `Surfel-GI` placement/debug behavior in `jEngine/Resource/Shaders/hlsl/SurfelGI_cs.hlsl`
- Session goal: identify why spawn-attempt dots appear but local cell coverage looks sparse.

## What We Verified
1. Yellow in `Show Spawn Attempt Points` originally meant min-separation reject.
2. After disabling min-separation reject in code, issue remained.
3. Cascade mismatch early-return (`currentCascade != cascadeIndex`) was identified as one blocker.
4. Disabling that mismatch return improved placement in user test.
5. Current dominant reject reason observed in latest color split: `NO_WRITABLE_SLOT`.

## Key Root-Cause Direction
- Problem is not one gate only.
- Current signal suggests bucket/slot pressure in hashed cell buckets:
  - creation path may be allowed,
  - but final write slot cannot be secured (`foundWritableSlot == false`).

## Active Code Changes (CURRENT)
File: `jEngine/Resource/Shaders/hlsl/SurfelGI_cs.hlsl`

1. Spawn-attempt high-contrast palette enabled.
2. `REJECTED_NO_REPLACE` was split into 3 explicit reject reasons:
- `ATTEMPT_COLOR_REJECTED_NO_CREATE_PATH` (red)
- `ATTEMPT_COLOR_REJECTED_OCCUPANCY_GATE` (black)
- `ATTEMPT_COLOR_REJECTED_NO_WRITABLE_SLOT` (neon green)
3. Final reject write now maps to the split reasons instead of one shared color.
4. Functional logic is otherwise baseline (except palette/reason-color instrumentation).

## Current Attempt Color Legend (as of this handoff)
- Gate pass: magenta
- Cascade mismatch: lime
- Merged: blue
- Dormant reused: orange
- Historical reused: purple
- Hysteresis wait: cyan
- Rejected min-separation: yellow
- Rejected no-create-path: red
- Rejected occupancy-gate: black
- Rejected no-writable-slot: neon green
- Spawn new: white
- Replaced far: gray
- Steal far: dark green

## Experiments Performed and Reverted
All reverted unless noted above:
1. Force `violatesMinSeparation=false`.
2. Force `canWriteByCellOccupancy=true`.
3. Force `shouldCreateOrReplace=true` and fallback write on no slot.
4. Limit min-separation check to current cell only.
5. Mismatch branch fallback-search for vacant slot.

## Useful Code Pointers
- mismatch early-return: around `currentCascade != cascadeIndex`
- create/replace gate: `shouldCreateOrReplace`
- occupancy gate reject: `canWriteByCellOccupancy`
- writable slot reject: `foundWritableSlot`

## Suggested Next Big Step
- Structural mitigation for hash/bucket contention:
1. per-cascade pool partition (or separate buffers), or
2. per-cascade independent bucket range with explicit base offsets, or
3. set-associative + secondary-hash slot selection.

## Workspace Notes
- Modified: `jEngine/Resource/Shaders/hlsl/SurfelGI_cs.hlsl`
- `handoff.md` updated for backup continuation.

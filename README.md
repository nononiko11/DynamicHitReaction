# Dynamic Hit Feedback — GTA San Andreas 1.0 US

Procedural, Euphoria-style bullet reactions for NPCs. Limbs react to *where* they are hit and *what* hit them, running peds
trip when shot in the leg, and the pop between the ragdoll and the vanilla get-up animation is smoothed out.
**Ragdolling itself is left to the Bullet Physics Ragdoll mod** (madleg / MixMods fix) — this plugin never simulates a ragdoll.

## What it does

| Feature | How |
|---|---|
| Hit-location reactions | The impact point is matched against the ped's skeleton (capsules around every bone). Hand → hand/forearm/arm whip back and the arm pulls in towards the body; calf/foot → leg kicks and the knee folds; chest → torso bends along the bullet with a twist for off-centre hits and the head lagging then whipping; head → neck/head snap. |
| Weapon weight | Per-weapon intensity ([Weapons] in the ini). Pistols/SMGs = light taps, Deagle/AK/M4 = jolts, shotguns/rifles/sniper = heavy blows. Intensity grows faster than linearly so the gap between light and heavy is obvious. |
| No vanilla flinches | For ~0.9 s after a bullet hits a ped the stock `DAM_*` / `SHOT_*` / `HIT_*` animations are faded out so only the procedural reaction shows. Knock-down anims (`KO_*`, `FALL_*`, `GETUP*`) are **not** touched — they are what the ragdoll mod keys on. |
| Trip | A running ped (move state ≥ run, or fast enough) hit in foot/calf — less likely thigh — stumbles forward with flailing arms, then falls through the game's own `TASK_FALL_AND_GET_UP` (see *CLEO bridge*). The ragdoll mod handles the fall, the vanilla get-up animation plays afterwards. |
| Get-up blend | Every frame the plugin knows what pose was last displayed. When that is far from what the animation wants (ragdoll just ended, get-up animation starts) it eases the skeleton from the displayed pose into the animation over ~0.35 s instead of snapping. |

## Requirements

* GTA San Andreas **1.0 US** (HOODLUM-decompressed or the compact exe) and an ASI loader.
* Bullet Physics Ragdoll (recommended — that is what makes falls ragdoll).
* CLEO 4 only for the trip → ragdoll hand-off (the ragdoll mod needs CLEO anyway).

## Build on GitHub (nothing to install)

1. Create a repository and upload everything in this folder, **including** `.github/workflows/build.yml`
   (hidden folder — if your uploader skips it, use *Add file → Create new file*, name it `.github/workflows/build.yml` and paste the contents).
2. Open the repository's **Actions** tab. The *Build DynamicHitFeedback* workflow runs on every push.
3. When it is green, open the run and download the **DynamicHitFeedback** artifact: it contains `DynamicHitFeedback.asi`,
   `DynamicHitFeedback.ini`, `DHF_TripBridge.txt` and this README.
   (Push a tag such as `v1.0.0` and the same files are attached to a GitHub Release.)

The workflow also runs `dev/run_selftest.sh` (logic tests on a synthetic skeleton) in a separate job.
Local build: `cmake -S . -B build -A Win32` then `cmake --build build --config Release` with Visual Studio 2019/2022.
It **must** be a 32-bit (`Win32`) MSVC build.

## Install

1. Copy `DynamicHitFeedback.asi` and `DynamicHitFeedback.ini` into the GTA SA folder (next to the ragdoll mod's files).
2. **Trip → ragdoll hand-off (CLEO bridge):** open `DHF_TripBridge.txt` in Sanny Builder (SA, CLEO script), compile, and save as
   `CLEO\DHF_TripBridge.cs`. Without it the stumble still plays but the fall is not handed to the ragdoll mod.
3. Play. `DynamicHitFeedback.log` appears next to the `.asi`.

### Why the bridge exists
The plugin can read and shape poses safely, but calling the game's task constructors from outside needs addresses that could not
be verified without the executable. Opcode `05BB` (`TASK_FALL_AND_GET_UP`) is the game's own, public way to do the same thing, so a
tiny CLEO script performs it on request (shared mailbox exported by the `.asi`). If no script ever answers, the plugin notices after
3 trips, stops posting requests and just plays the stumble (`UseCleoBridge=0` does the same).

## How it plugs into the game

* `CWeapon::DoBulletImpact` (0x73B550): observed, never altered — gives victim, weapon, impact point and direction.
* `CEntity::UpdateRpHAnim` (0x532B20): runs right before the engine converts animation key frames into bone matrices.
  The plugin edits the *interpolated key frames* (local bone rotations) there, so skinning, attached weapons and shadows all follow.
  Because the Bullet Physics Ragdoll mod writes the final pose itself, it always has the last word on ragdolled peds; dead peds are
  never touched.
* Every call site of those two functions is retargeted by scanning the executable (no hard-coded call addresses).
* Every hook body runs inside an exception guard; three faults switch the mod off for the session instead of crashing the game.
* **Self-checks at runtime:** exe version, skeleton layout, how RenderWare's quaternions map onto the bone matrices, and animation
  struct layouts are all *measured* from live peds before anything is modified. If a check fails the affected feature disables
  itself and the log says why.

## Tuning (`DynamicHitFeedback.ini`)

* `GlobalIntensity`, `ArmFlinch`, `LegFlinch`, `TorsoBend`, `HeadSnap`, `MaxAngleScale` – how strong/large reactions are.
* `[Weapons]` – intensity per weapon id (0 disables a weapon).
* `[Trip]` – `MinMoveState`, per-class `Chance*`, `CooldownMs`, `GroundTimeMs`.
* `[GetUpBlend]` – `BlendTime`; `WatchAlways=1` if your ragdoll build does not use the `FALL_*`/`KO_*`/`GETUP*` animations.
* `AffectPlayer=1` also reacts CJ (off by default; experimental).

## Troubleshooting (read `DynamicHitFeedback.log`)

| Log says | Meaning / fix |
|---|---|
| `unsupported executable` | Not GTA SA 1.0 US. Nothing was patched. |
| `hook UpdateRpHAnim: 0 call site(s)` or *"hook has never run"* | Another plugin took over those calls. Rename the `.asi` so the load order changes (ASIs load alphabetically). |
| `calibration FAILED` | The bone data did not look like expected. Send the log. Reactions stay off, nothing is modified. |
| `skeleton of ped … not usable yet: …` followed by a hex dump | Layout differs from what was assumed. Send the log — the dump is what is needed to fix it. |
| `bridge: script never answered` | `DHF_TripBridge.cs` is not installed/compiled. |
| `anim suppression disabled` | Animation struct did not match; stock flinches will play. |
| `FAULT (access violation)` | Caught and counted; three faults disable the mod. Send the log. |

## Honest limitations

* This was written without access to a running game. The logic (maths, calibration under both quaternion conventions, hit directions,
  weapon scaling, idempotency, trip handshake, get-up blending, fail-safes) is covered by 268 automated checks on a synthetic ped, and the
  engine addresses were cross-checked against public references. How the *real* ragdoll mod reacts to the `05BB` fall, and whether
  its pose lands where the blend expects it, can only be confirmed in-game.
* Heavy weapons can still trigger the vanilla/ragdoll knock-down on their own; those are left to the ragdoll mod.
* The reaction starts `HitDelayMs` (34 ms) after the shot so that killing shots, which belong to the ragdoll mod, are skipped.
* Tuning values (frequencies, angles) are deliberately conservative starting points.

## Developer notes

`dev/selftest.cpp` runs the real `Skeleton`/`Reactions` code against a fake ped in 32-bit-addressable memory
(`bash dev/run_selftest.sh`, add `--report` to the binary for a table of reaction magnitudes per weapon/region).
Layout:

```
src/main.cpp        DllMain, call-site retargeting, hook thunks (SEH guarded)
src/GameSA.*        addresses, version check, memory/code patching helpers
src/Skeleton.*      bone hierarchy access, layout probing, convention calibration
src/Reactions.*     per-ped state, springs, hit recipes, trip, suppression, get-up blend
src/Config.*        ini loading        src/Log.*   log file
cleo/               DHF_TripBridge.txt (Sanny Builder source)
```

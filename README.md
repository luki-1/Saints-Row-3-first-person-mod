# SRTT First Person 1.11

A first-person camera for **Saints Row: The Third** (original 2011 PC release, DX9 and DX11), plus a
level-of-detail distance multiplier. Press **L** in game to switch between first and third person.

## Install / uninstall

Double-click `install.bat` in the `MOD` folder. It finds the game (the folder `MOD` is in, or your Steam
libraries, or it asks), then copies the mod's two files next to `SaintsRowTheThird.exe`:

- `dinput8.dll` (the mod)
- `SRTT_FirstPerson.ini` (settings; an existing one is kept, so updating keeps your settings)

If another mod's `dinput8.dll` is already there (an ASI loader, for example), the installer asks before
renaming it to `dinput8.dll.before-srtt-fp`; uninstalling puts it back. Close the game before installing.

To uninstall, run `install.bat /uninstall` (it asks whether to delete your settings too), or delete
`dinput8.dll` from the game folder by hand. No game files are modified.

To rebuild: run `build.bat` in `SRTT_FirstPerson` (needs the Visual Studio C++ build tools), then copy
`bin\dinput8.dll` into the `MOD` folder and run the installer.

## What it does

- On foot (walking, sprinting, crouching, aiming, melee, climbing over fences, short falls, scripted
  mission walking) the camera sits at your character's eyes and follows the head as it moves. Right-click
  aiming zooms in a little instead of going over the shoulder. Scoped weapons work as before.
- In vehicles (cars, bikes, boats, helicopters, planes, tanks, and aiming out of a vehicle) the camera sits
  at the driver's eyes. While driving, the view turns with the vehicle and stays wherever you point it with
  the mouse: the game's swing back to face forward (and its lock behind aircraft) is off in first person.
- Swimming, ragdoll, skydiving/parachuting, human shields, takedowns, remote-controlled vehicles and co-op
  downed/spectator keep the normal third-person camera; the view glides out to it and back to the eyes.
- Cutscenes and scripted cameras are unaffected.
- `LODScale` (default 4) keeps detailed models that many times further away for characters, level objects
  (props, street furniture, building details), instanced meshes, vehicles and items such as weapons.

## Settings (`SRTT_FirstPerson.ini`)

| Key | Default | Meaning |
| --- | --- | --- |
| `ToggleKey` | `L` | A letter/digit, or a virtual-key code such as `0x74` for F5 |
| `StartEnabled` | `0` | `1` starts the game in first person |
| `VehicleFirstPerson` | `1` | `0` keeps the normal camera in vehicles |
| `VehicleAutoCenter` | `0` | `1` keeps the game's swing back to face forward while driving in first person |
| `AttachToHead` | `1` | Follow the character's animated head; `0` uses a fixed height above the feet |
| `HideHead` | `1` | Hide the local player's head, eyes, hair and head accessories in first person; restore in third person |
| `HeadUp`, `HeadForward` | `0.08`, `0.02` | Eye position relative to the head, in metres (forward = the way you look) |
| `LookDownForward` | `0.20` | Extra forward clearance on foot, fading in from 45 to 75 degrees down; 0 disables it (max 0.40 m) |
| `HeadSmoothing` | `0.08` | Damps head bob, in seconds; `0` turns it off |
| `EyeHeight`, `CrouchEyeHeight` | `1.60`, `1.30` | Camera height above the feet when not following the head |
| `FOV`, `SprintFOV`, `AimFOV`, `VehicleFOV` | `82.8`, `91.08`, `55.2`, `82.8` | Field of view in first person; `0` keeps the game's value |
| `LookUpLimit`, `LookDownLimit` | `80`, `80` | Pitch limits in degrees; `0` keeps the game's (75 up, 55 down on foot) |
| `NearClip` | `0.03` | First-person near plane in metres (0.02 to 1); 0 keeps the game's value |
| `LODScale` | `4` | Level-of-detail distance multiplier (0.25 to 16); `1` = the game's distances |
| `Log`, `Debug` | `1`, `0` | Write `SRTT_FirstPerson.log`; `Debug=1` also logs the camera state every second |

Changes take effect the next time the game starts.

## How it works

Both game executables import `DirectInput8Create` from `dinput8.dll`, so Windows loads this proxy from the
game folder first; it forwards everything to the real system `dinput8.dll`.

### VTOL flight-mode look input and smoothing rollback (1.11)

The arm/weapon stabilizer introduced in 1.10 has been removed, including its rendering hooks and
`WeaponSmoothing` setting. Arms, weapons and body rendering use the previous native transforms.
The original camera head-bob smoothing is retained. Player rotation remains disabled (`FaceCamera=0`).

The previous pitch-limit repair did not address a separate input restriction: when mouse flight
controls are enabled, the vehicle look-input callback returns early for airplanes and VTOL flight
mode, discarding both look axes. This release bypasses that one predicate call only for the current
first-person vehicle camera with `VehicleAutoCenter=0`. The native input processing, sensitivity,
inversion and pitch integration then run normally. Flight physics and other aircraft classification
calls are unchanged; third person, vehicle aiming and other camera targets keep their original routing.

Tests reproduce the original rejection and execute the patched input route plus native pitch
integration from both DX9 and DX11, including repeated hover/flight transitions, both pitch directions
and inversion. This is offline verification; the full VTOL flight-mode view still needs an in-game check.

### Aircraft hover/flight switching (1.9)

With first-person vehicle free look enabled (`VehicleAutoCenter=0`), the aircraft mode-change
forced-angle request is bypassed for the current vehicle. Other forced-look callers and third-person
views keep their native behavior. Configured up/down limits are reapplied to the vehicle controller's
cached pitch bounds after its update, so a mode switch cannot leave the view clamped in place.
Setting either look limit to `0` still leaves that limit under game control. Vehicle aiming cameras
retain their native weapon-camera bounds. Player-facing changes remain disabled in the supplied INI.

Offline tests validate the call-site signature on DX9/DX11, controller resets across driver/alternate/
helicopter/airplane modes, preservation of native values, and the forced-look bypass boundaries.
Hover/flight switches in the Oppressor and VTOL still require visual testing in game.

### Moving vehicle head visibility (1.8)

Vehicle head filtering now accepts the current eye position and the immediately preceding eye
position in the same vehicle camera mode. This addresses the movement-only failure where rendering
can use a previous camera sample more than 15 cm from the latest update. Each sample retains the
15 cm tolerance; positions between the samples are not treated as first person. History is discarded
on camera mode/player changes, disabled first person, missing eye updates, or updates over 100 ms.
Shadows still use the complete head. `FaceCamera=0` remains in the supplied configuration.

The regression test drives the camera and draw hooks through five vehicle camera modes and five
speeds, checking head/hair filtering, body/shadow retention, external views and history expiry.
The new build needs an in-game driving/flying check. With `Debug=1`, the log reports the distance
between each vehicle render view and the two eye samples once per second.

### Camera

The free camera is driven by the camera "submodes" from `camera_free.xtbl`, which the game parses into a
table holding, per submode, a look-at offset, pitch limits, FOV, orbit distance and shoulder shift. When you
get into a vehicle the game writes that vehicle's camera settings (`vehicle_cameras.xtbl`) into its
submode. The game's own sniper-scope `zoom` submode is already a first-person camera (eye-height look-at,
0.01 m distance, no shift). While first person is on, the mod gives the on-foot and vehicle submodes those
values (vehicles keep their own pivot) and updates the camera controller's cached copy so the switch is
instant. Values the game writes into those submodes meanwhile are remembered, so toggling back restores
exactly what the game would have.

That camera pivots on a fixed point, and the character's head moves well away from it (leaning while
running, sitting in a car). So after the game has computed the camera each frame, the mod asks the game for
the current position of the player's `head` rig tag, the same lookup the game uses to aim scripted cameras
at characters, and moves the camera there, slightly up and forward to the eyes. While the game blends
between a first-person submode and a third-person one, the mod eases between the two positions using the
game's own blend progress.

The camera's heading is kept in world space. While you drive, the game turns it back towards the direction
of travel (after a few seconds without mouse input, or straight away for some vehicles) and eases the pitch
back to level; some aircraft get the heading locked behind them. While driving in first person the mod
skips both, and each frame turns the heading by however much the vehicle turned, measured the same way the
game measures it for its own vehicle-relative camera. So the view keeps its angle to the vehicle until you
move the mouse. Aiming out of a vehicle keeps the game's behaviour.

### Animation visibility, shadows and body facing (1.7)

Head hiding filters every mesh material/depth draw in the eye view. Shadow rendering is identified by
an explicit, thread-local scope around the game's shadow render pass. This corrects the flat head/eye
silhouettes in 1.6, whose filter incorrectly excluded some non-shadow depth draws. The shared model
remains complete for shadows. Checking the render camera's actual world position restores the head
when an animation camera pulls away, even if its free-camera submode is unchanged.

`FaceCamera=1` (optional; shipped configuration uses `0`) selects the game's native view-relative facing and directional animation
paths for the local player in normal on-foot first person. It enables the directional selector's
existing left/right/backward angle thresholds and hysteresis. Movement vectors and speed remain
under the game's control. The direct late-frame physics rotation from 1.6 has been removed; that
method fought movement and left the animation controller selecting forward running.

Vehicles, scripted camera modes and non-upright poses retain normal control. The native animation
state machine still owns special moves and sprint animations. `FaceCamera=0` restores normal facing.
The previous look-down neck clearance, weapon near plane and custom FOV settings are retained.

Both DX9 and DX11 offline checks pass, including execution of the native directional selector and
facing-offset calculations. In-game appearance, movement and animation transitions need visual testing.

### Look-down neck clearance (1.5)

With head hiding enabled, the camera moves up to 20 cm further forward horizontally as you look
from 45 to 75 degrees down. This keeps the open neck behind the viewpoint without adding geometry.
It follows your viewing direction, blends smoothly, and leaves level/upward views and vehicle cameras
unchanged. `LookDownForward=0` disables it. It requires `AttachToHead=1` and `HideHead=1`.
This is a view-position workaround: some outfits or downward weapon animations may still need tuning.

### Head visibility and weapon clearance (1.4)

Head/face VIDs 1 and 50�53 and head accessories are hidden only in the eye view. These are the
body regions obscured by the game's full-face STAG helmet. VID-to-part mapping is resolved for the
current mesh and LOD. Outfit data, skinning bones and game files are unchanged.

The first-person near plane is now 3 cm instead of 15 cm, and the default forward eye offset is 2 cm
instead of 10 cm, giving held weapons more clearance. The original near plane is restored when leaving
first person. This reduces near-plane slicing; third-person reload/melee animations can still physically
pass through the viewpoint. `NearClip=0.15` restores the previous depth precision if distant surfaces flicker.

### Level of detail

The game has several separate LOD systems; `LODScale` multiplies each one's distances:

- **Characters**: the default LOD distances (10/25/50/100 m) that every character definition starts from.
- **Level objects**: the medium/low LOD distances of the fade categories, both the five built into the game
  and those in `fade_categories.xtbl` (fade-out distances are left as they are).
- **Instanced meshes** (only drawn this way at high scene detail): the camera distances used to pick LODs
  and the shader fade parameters, so both stay consistent.
- **Vehicles**: the `LOD_DistanceRatio` lists (distance divided by vehicle size), including the defaults;
  lists a vehicle copies from its defaults or template are only scaled once.
- **Items** (`items_3d.xtbl`, e.g. weapons): each LOD distance.

The city itself (which blocks are streamed in at full detail) is not affected; that is limited by memory.

Game code and data are found by byte signatures at startup. If anything does not match (for example a
different game version), the mod writes the reason to `SRTT_FirstPerson.log` and leaves that part alone.

## Known limitations

- Your own body is visible when you look down. Head geometry is hidden with `HideHead=1`; arms or weapons can still pass through the view during
  some animations. Vehicle interiors were never meant to be seen this close.
- In first person the view turns with a vehicle's heading but not its pitch or roll, so in aircraft the
  horizon stays level (`VehicleAutoCenter=1` brings back the game's lock behind aircraft).
- Large `LODScale` values cost frame rate, especially in dense areas.
- Other mods that also ship a `dinput8.dll` (for example ASI loaders) conflict with this one.

## Development checks

`tests\run.bat` builds an x86 harness that executes real VID lookup from both installed executables
against synthetic meshes with different LOD mappings. It checks all 48 tested material-pass/draw-flag
combinations through the actual draw hook, shadow-scope restoration (including exceptions), animation
camera restoration, local-player isolation, the assembly strafe hook, unchanged shared render data,
near-plane ownership and look-down clearance.

`tests/native_locomotion.py` emulates the native facing-offset calculation and directional-animation
selector from each executable for four movement directions at four camera headings. It verifies the
left/right/backward animation flags and that requested movement direction/speed are unchanged. This
uses `pefile` and `unicorn` from `.tools`. These checks do not run a complete game frame or replace
visual testing of weapons, outfits and animation transitions in game.

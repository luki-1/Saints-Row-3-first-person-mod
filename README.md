# SRTT First Person

A first-person camera for **Saints Row: The Third** (original 2011 PC release, DX9 and DX11), plus a
level-of-detail distance multiplier. Press **L** in game to switch between first and third person.

https://www.youtube.com/watch?v=_ErMo5DBjGA

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
| `HeadUp`, `HeadForward` | `0.08`, `0.10` | Eye position relative to the head, in metres (forward = the way you look) |
| `HeadSmoothing` | `0.08` | Damps head bob, in seconds; `0` turns it off |
| `EyeHeight`, `CrouchEyeHeight` | `1.60`, `1.30` | Camera height above the feet when not following the head |
| `FOV`, `SprintFOV`, `AimFOV`, `VehicleFOV` | `82.8`, `91.08`, `55.2`, `82.8` | Field of view in first person; `0` keeps the game's value |
| `LookUpLimit`, `LookDownLimit` | `80`, `80` | Pitch limits in degrees; `0` keeps the game's (75 up, 55 down on foot) |
| `NearClip` | `0.15` | Near clip distance in first person (the game's own value); raise it if bits of your head show |
| `LODScale` | `4` | Level-of-detail distance multiplier (0.25 to 16); `1` = the game's distances |
| `Log`, `Debug` | `1`, `0` | Write `SRTT_FirstPerson.log`; `Debug=1` also logs the camera state every second |

Changes take effect the next time the game starts.

## How it works

Both game executables import `DirectInput8Create` from `dinput8.dll`, so Windows loads this proxy from the
game folder first; it forwards everything to the real system `dinput8.dll`.

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

- Your own body is visible when you look down, and arms or weapons can pass close to the view during
  some animations. Vehicle interiors were never meant to be seen this close.
- In first person the view turns with a vehicle's heading but not its pitch or roll, so in aircraft the
  horizon stays level (`VehicleAutoCenter=1` brings back the game's lock behind aircraft).
- Large `LODScale` values cost frame rate, especially in dense areas.
- Other mods that also ship a `dinput8.dll` (for example ASI loaders) conflict with this one.

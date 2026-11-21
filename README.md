# Saints Row: The Third — First Person Mod

Turn Steelport into a first-person playground.

This mod adds a fully playable first-person camera to the original PC release of **Saints Row: The Third**. Walk the streets, kick in doors, drive through traffic, fly VTOLs, and see the world from the Boss's point of view — while keeping the game's third-person camera only a key press away.

> Built for the original 2011 PC version of Saints Row: The Third, including both DX9 and DX11 executables.

## Highlights

- **Instant first / third-person switching** — press **L** whenever you like.
- **A real body-aware view** — look down to see your character, clothes, hands, and held weapons.
- **First-person vehicles** — cars, bikes, boats, tanks, helicopters, planes, and VTOLs all put you in the driver's seat.
- **Free-look driving and flying** — look around naturally instead of being forced back behind the vehicle.
- **Action-friendly camera** — aiming, melee, climbing, swimming, parachuting, takedowns, cutscenes, and other special moments use the view that looks best.
- **Sharper Steelport at distance** — optional extended level-of-detail settings keep more of the city detailed further away.

## Installation

1. Download and extract the release.
2. Double-click `install.bat`.
3. Start the game and press **L**.

The installer locates your game automatically and copies only the mod files beside `SaintsRowTheThird.exe`. It does **not** alter any original game files. Close the game before installing.

To remove it, run `install.bat /uninstall` from the folder you extracted.

## Controls

| Key | Action |
| --- | --- |
| **L** | Switch between first and third person |
| **Mouse / controller look** | Look around |
| **Right mouse / aim** | Aim as usual |

The toggle key can be changed in `SRTT_FirstPerson.ini`.

## Make it yours

Open `SRTT_FirstPerson.ini` in a text editor to tune the experience. The defaults are designed to work out of the box, but you can easily change:

- whether first person starts enabled;
- the camera's field of view;
- first-person driving and flying;
- head bob and eye position;
- how close objects can get to the camera; and
- the detail range for Steelport.

Most changes take effect the next time you launch the game. If you only want the default experience, you never need to edit this file.

## What to expect

First person is used where it feels great, and the game smoothly returns to third person for cinematic or unusual situations such as cutscenes, takedowns, ragdolls, human shields, remote-control vehicles, and co-op spectator states. Your character's head is hidden only in the close first-person view, while their normal shadow remains.

Like many first-person mods for third-person games, a few original animations and vehicle interiors were never made to be viewed from this close. If an animation looks awkward, tap **L** to switch back at any time.

## Compatibility

- Works with the original PC release of **Saints Row: The Third** (DX9 and DX11).
- Other mods that include their own `dinput8.dll` can conflict with this mod. The installer offers to preserve an existing one, but only one can be active at a time.
- Large detail-range settings can reduce performance in busy parts of Steelport.

## Troubleshooting

If the mod does not load, make sure `dinput8.dll` and `SRTT_FirstPerson.ini` are in the same folder as `SaintsRowTheThird.exe`. The file `SRTT_FirstPerson.log` in that folder can help identify a version mismatch or installation problem.

## Credits

Created for Saints Row: The Third fans who have always wanted to experience Steelport from inside the action.

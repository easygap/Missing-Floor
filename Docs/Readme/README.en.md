[한국어](../../README.md) · **English** · [日本語](README.ja.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md)

# Missing Floor

Your brother's last address was Unit 501, Moonlight Villa. The building only has four floors.

Missing Floor is a first-person horror game set in an old apartment building in Seoul. You play Yudam, who moves into Unit 403 to look for her missing brother. By day you talk to the neighbors and dig through records. Before dawn you stay out of the way of whatever comes down from above the ceiling. It's being made in Unreal Engine 5, and a free Windows playtest is up.

![A four-story building at the end of a wet alley, two windows lit](../Media/readme/title-menu-first-run-1080-en.webp)

The current build is 0.9.1 for Windows. Text is available in English, Korean, Japanese, and Simplified and Traditional Chinese.

[Download the playtest (Windows, 0.9.1)](https://github.com/easygap/Missing-Floor/releases/download/v0.9.1/MissingFloor-0.9.1-Windows.zip) · [Trailer](https://github.com/easygap/Missing-Floor/releases/download/v0.9.0/MissingFloor-Trailer-en.mp4) · [Controls](#controls)

## Day

At 4:30 a.m. on the first night, three knocks come from the ceiling. Unit 403 is on the top floor.

In daylight you follow your brother's trail around the building and the alley. Mrs. Hwang in 401 has lived here for thirty years. Narin, who works at the convenience store across the alley, knows everyone's business.

A bulk waste notice is taped to the management office door, its pickup date crossed out and moved up. Jeongwoo from 202 is hauling the "unclaimed" boxes out of the office and up to 302. One of them has your brother's handwriting on it.

![The fourth-floor corridor. Gas inspection stickers and old flyers on every door.](../Media/readme/game-corridor-day-en.webp)

<table>
  <tr>
    <td width="50%"><img src="../Media/readme/game-alley.webp" alt="The alley outside the building, a convenience store sign at the far end"></td>
    <td width="50%"><img src="../Media/readme/game-store.webp" alt="The counter at the Dawn24 convenience store"></td>
  </tr>
  <tr><td>The alley</td><td>Dawn24</td></tr>
</table>

The management office ledger, the CCTV and the meter readings on the ground floor don't quite agree. What you hear through the walls counts too. Press `Tab` to reread your notes and what people told you. If you're stuck, press `H` to hear Yudam's thoughts on where to look next.

Signs and notices stay in Korean. Story documents can be read in English when you examine them.

<table>
  <tr>
    <td width="50%"><img src="../Media/readme/p1-meter-cabinet-en.webp" alt="Electricity meters and reading cards for each unit"></td>
    <td width="50%"><img src="../Media/readme/game-booth.webp" alt="The management office desk with the ledger and CCTV monitor"></td>
  </tr>
  <tr><td>Meter cabinet</td><td>Management office</td></tr>
</table>

[Walking through the building and the neighborhood (GIF, 3.0 MB)](../Media/readme/readme-route-preview-en.gif)

## 4:30 a.m.

From 4:30 to 5:30 the building is locked, and the one upstairs crawls the corridors. It can't see, but it hears. Run or slam a door and it comes your way. When it passes right next to you, you hold your breath.

Stand in the dark too long and a black shape appears, and it grows the longer you look at it. If a familiar voice calls from outside your front door, don't open it.

In the back room of the management office, in 302 and on the fifth-floor wall, someone has packed layer after layer of soundproofing. Whatever is stuck there ignores sound and light. It looks for gaps that lead into the wall and covers the widest one with its body. Sometimes you have to open a hatch to draw it somewhere else, or wedge up a fallen board to clear a path. Get in its way and you'll be pinned between the board and its body.

![The fifth-floor wall. A mass of soundproofing and steel has stretched itself up to the ceiling.](../Media/readme/game-annex-lining.webp)

![A dark corridor. Light from a neighbor's unit leaks under a door.](../Media/readme/game-corridor-night-en.webp)

You can hide in the wardrobe or under the bed, and block the way with the front-door latch or the stairwell fire doors. Flashlight batteries come from the convenience store.

Getting caught doesn't send you back to the start of the night. You come to wherever you were dragged, a few minutes later. What you lose depends on what caught you. If it was the one upstairs, your flashlight is lying on the floor where it grabbed you, and you have to go back there for it.

Once you have found what the night had to show you, you can lie down in the bed in Unit 403 and wait for morning. You can keep looking around until 5:30, but if the night’s work isn’t done by then, you go through that night again the next evening.

![Unit 403, boxes still half unpacked. You can hide under the bed.](../Media/readme/game-bedroom-en.webp)

[A chase in the corridor (GIF, 3.6 MB)](../Media/readme/night-listener-chase.gif)

## Download

The current playtest is 0.9.1, a candidate for 1.0 that is being checked on more PCs and with first-time players before the full release.

1. Download the [Windows ZIP](https://github.com/easygap/Missing-Floor/releases/download/v0.9.1/MissingFloor-0.9.1-Windows.zip).
2. Extract everything and run `MissingFloor.exe`. The `Engine` and `IndieGame` folders have to stay next to it.
3. Select Start Game. Progress saves automatically, and Continue picks up where you left off.

The executable isn't signed, so Windows may show "Windows protected your PC." Select More info, then Run anyway.

The game starts in your Windows display language, and you can change it in Settings. The PC it was tested on and what to try if it won't start are in the [setup guide](../PLAYING.md) (Korean).

## Controls

| Key | Action |
|---|---|
| WASD, mouse | Move, look around |
| Left Shift, C, Space | Run, crouch, jump |
| E | Examine, open doors, hide |
| Hold E | Open or close a door quietly, listen at a wall |
| Q | Knock |
| Left Ctrl | Hold your breath |
| F | Flashlight |
| F1 | Objective and controls |
| Tab, H | Journal (daytime), hint |
| ← →, mouse wheel | Turn pages |
| Esc, F10 | Pause, accessibility settings |

Controllers work too. Keys and buttons can be remapped in Settings.

## Difficulty and accessibility

If being chased is too much, press `F10` and set the difficulty to No Chase. Nothing catches you and you can follow the story and puzzles to the end. Every ending is reachable on every difficulty.

Caption size and background, sound direction markers, camera shake, flickering lights and lens effects can all be adjusted. Held buttons can be switched to toggles. Microphone input is off unless you turn it on.

![The accessibility settings screen](../Media/readme/settings-accessibility-20260929-en.webp)

## Credits

Sound effects are CC0 assets from OpenGameArt and Kenney, surface textures come from ambientCG, and scanned props from Poly Haven, also CC0. The fonts are Pretendard and Gowun Batang, with Noto Sans CJK for Japanese and Chinese (all SIL Open Font License 1.1).

Some of the game is AI-made. Parts of the printed material, signs and textures are AI-generated images edited by hand. The bodies of the people and creatures, such as the one upstairs, the black shape, the building manager and the neighbors in 303 and 202, along with the alley cat and the delivery scooters, were turned into 3D from AI images, then cleaned up and rigged in Blender. The thing stuck to the walls was built by hand in Blender from AI concept images and texture tiles. Many other props were modeled in Blender from AI concept images. Full sources and terms are in the [asset ledger](../ASSET_POLICY.md) (Korean).

## Bugs and feedback

If the game freezes or does something odd, [file a bug](https://github.com/easygap/Missing-Floor/issues/new?template=bug_report.yml) with where you were and what happened. [Thoughts on the playtest](https://github.com/easygap/Missing-Floor/issues/new?template=feedback.yml) are welcome too.

To build from source, see "소스에서 빌드하기" in the [setup guide](../PLAYING.md#소스에서-빌드하기). You'll need Unreal Engine 5.8, Visual Studio with the "Game development with C++" workload, the Windows SDK and Git LFS.

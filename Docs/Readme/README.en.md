[한국어](../../README.md) · **English** · [日本語](README.ja.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md)

# Missing Floor

A first-person horror game set in a run-down apartment building in Seoul, made in Unreal Engine 5. There's a free playtest for Windows.

![A four-story building at the end of a wet alley, two windows lit](../Media/readme/title-menu-first-run-1080-en.webp)

The returned parcel lists your brother's address as Unit 501, Moonlight Villa.
The building only has four floors.

On your first night in Unit 403, at 4:30 in the morning,
someone knocks three times on a ceiling with nothing above it.

[Download the playtest (Windows, 0.2.4)](https://github.com/easygap/Missing-Floor/releases/download/v0.2.4/MissingFloor-0.2.4-Windows.zip) · [Trailer](https://github.com/easygap/Missing-Floor/releases/download/v0.2.3/MissingFloor-Trailer.mp4) · [Controls](#controls)

## By day

All you have from your brother is the returned parcel and one voicemail. Mrs. Hwang in 401 has lived in the building for thirty years, and the clerk at the convenience store across the alley hears most of the neighborhood gossip. Somebody might remember him.

<table>
  <tr>
    <td width="50%"><img src="../Media/readme/game-alley.webp" alt="The alley outside the building, a convenience store sign at the far end"></td>
    <td width="50%"><img src="../Media/readme/game-store.webp" alt="The counter at the Dawn24 convenience store"></td>
  </tr>
  <tr><td>The alley</td><td>Dawn24</td></tr>
</table>

The ledger in the management office doesn't match the CCTV log. Count the meters. Put your ear to the walls. `Tab` brings back everything you've read, and `H` gives you a hint if you're stuck.

Shop signs, flyers and the notices on the walls are in Korean, as they would be in Seoul. Anything you actually need to read is translated when you examine it.

## At night

From 4:30 to 5:30 every morning, some things in the building are awake.

The one upstairs can't see. It follows sound. Don't run, and open doors slowly. If it passes close, hold your breath.

Stay in the dark too long and the dark starts to gather. It grows the longer you look at it. Look away and get to a light.

If someone at the door calls out in a voice you know, don't open it.

![The management office desk at night, under a flashlight](../Media/readme/game-booth.webp)

You can hide in the wardrobe or under the bed, latch the front door, or shut the fire door to the stairwell. Flashlight batteries run down, so pick up spares at the convenience store.

Getting caught doesn't end the game. You wake up back in your room. Whatever you found stays found, but the clock keeps running.

![Unit 403 at dawn](../Media/readme/game-bedroom-dawn.webp)

[A chase in the corridor (GIF, 4.8 MB)](../Media/readme/night-listener-chase.gif)

## Download

The current build is playtest 0.2.4. Hiding, the latch and fire door, flashlight batteries and the other things that come at night will be in the next playtest. The second and third floors open up then too.

1. Download the [Windows ZIP](https://github.com/easygap/Missing-Floor/releases/download/v0.2.4/MissingFloor-0.2.4-Windows.zip).
2. Extract the whole thing and run `MissingFloor.exe`. It won't start without the `Engine` and `IndieGame` folders next to it.
3. Choose Start Game. Progress saves on its own, and Continue picks up where you left off.

If a "Windows protected your PC" window appears, click More info, then Run anyway. The executable isn't signed, which is why that shows up.

The game runs in English, Korean, Japanese, Simplified Chinese and Traditional Chinese. It starts in your Windows language, and you can change it in Settings. The [setup guide](../PLAYING.md) is in Korean only for now. It lists the PC the game has been tested on and what to try if it won't start.

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

Gamepads work too. Keys and buttons can be remapped in Settings.

## Difficulty and accessibility

If the chases are too much, press `F10` and set the difficulty to No Chase. Nothing will catch you, and you can still finish the story and the puzzles. Every ending can be reached on every difficulty.

Caption size and background, sound direction cues, camera shake, light flicker and the camera texture can all be adjusted. If holding keys is uncomfortable, change Hold Actions so a single press starts an action. Microphone input is optional and off by default.

![The accessibility settings screen](../Media/readme/settings-accessibility-20260929-en.webp)

## Credits

The sound effects are CC0 packs from OpenGameArt and Kenney, reworked for the game. Photo textures come from ambientCG and the scanned props from Poly Haven, all CC0. The fonts are Pretendard and Gowun Batang, both under the SIL Open Font License 1.1. Some of the printed material and reference art started out as image generator output and was retouched afterwards. Sources and licenses for every asset are in the [asset ledger](../ASSET_POLICY.md) (in Korean).

## Bugs and feedback

If the game crashes or something behaves oddly, please [file a bug report](https://github.com/easygap/Missing-Floor/issues/new?template=bug_report.yml) with where it happened and what you saw. [Impressions and suggestions](https://github.com/easygap/Missing-Floor/issues/new?template=feedback.yml) are welcome too. It's still rough in places.

To build from source, see "소스에서 빌드하기" in the [setup guide](../PLAYING.md#소스에서-빌드하기). You'll need Unreal Engine 5.8, Visual Studio with the "Game development with C++" workload, the Windows SDK and Git LFS.

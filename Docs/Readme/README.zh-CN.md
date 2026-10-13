[한국어](../../README.md) · [English](README.en.md) · [日本語](README.ja.md) · **简体中文** · [繁體中文](README.zh-TW.md)

# Missing Floor

哥哥最后留下的地址是月光公寓501室。可那栋楼只有四层。

这是一款以首尔老居民楼为舞台的第一人称恐怖游戏。你扮演为寻找失踪的哥哥而搬进403室的侑潭，白天向邻居打听、翻查楼里的记录，天亮前则要躲开从天花板上面下来的东西。游戏用 Unreal Engine 5 开发，Windows 测试版可以免费下载。

![雨后小巷尽头的四层公寓楼，只有两扇窗亮着灯](../Media/readme/title-menu-first-run-1080-zh-Hans.webp)

目前能玩的是 0.9.1 测试版（Windows），支持简体中文、繁体中文、韩语、英语和日语。

[下载测试版（Windows，0.9.1）](https://github.com/easygap/Missing-Floor/releases/download/v0.9.1/MissingFloor-0.9.1-Windows.zip) · [预告片（英文字幕）](https://github.com/easygap/Missing-Floor/releases/download/v0.9.0/MissingFloor-Trailer-en.mp4) · [操作说明](#操作说明)

## 白天

搬来的第一晚，凌晨四点半，天花板上传来三下敲击声。403室在顶层。

天亮后，就沿着哥哥留下的痕迹在楼里和小巷里转。401室的奶奶在这栋楼住了三十年，在小巷对面便利店上班的娜琳对街坊的事很清楚。

![403室所在的四楼走廊，每扇门上都贴着燃气检查贴纸和没撕掉的传单](../Media/readme/game-corridor-day-zh-Hans.webp)

<table>
  <tr>
    <td width="50%"><img src="../Media/readme/game-alley.webp" alt="公寓门前的小巷，尽头能看到便利店招牌"></td>
    <td width="50%"><img src="../Media/readme/game-store.webp" alt="黎明24便利店的收银台"></td>
  </tr>
  <tr><td>公寓门前的小巷</td><td>黎明24便利店</td></tr>
</table>

管理室的账本、监控录像和一楼电表上的记录，总有对不上的地方。贴着墙听到的声音也是线索。读过的记录按 `Tab` 可以再看，卡住时按 `H`，侑潭会自言自语地提醒该看哪里。

场景里的招牌和告示保留韩文，和剧情有关的文件调查时会显示中文。

<table>
  <tr>
    <td width="50%"><img src="../Media/readme/p1-meter-cabinet-zh-Hans.webp" alt="各户的电表和抄表单"></td>
    <td width="50%"><img src="../Media/readme/game-booth.webp" alt="放着账本和监控显示器的管理室桌子"></td>
  </tr>
  <tr><td>一楼电表箱</td><td>管理室</td></tr>
</table>

[在楼里和附近走动的画面（GIF，3.0 MB）](../Media/readme/readme-route-preview-zh-Hans.gif)

## 凌晨四点半

凌晨四点半到五点半，楼门被锁上，楼上的人在走廊里爬来爬去。它看不见，却听得见。跑起来或用力关门，它就会朝那边过来；它从身边经过时，只能屏住呼吸。

在没开灯的地方待太久，会出现一个黑影，越盯着它看就越大。门外就算传来熟人的声音，也不能开门。

![熄了灯的走廊，邻居家的灯光从门缝下漏出来](../Media/readme/game-corridor-night-zh-Hans.webp)

可以躲进衣柜或床底下，也可以用玄关门闩和楼梯间的防火门挡住去路。手电筒的电池白天在便利店买。

被抓住也不会从那一晚的开头重来。醒过来时人在被拖走的地方，中间已经过去了几分钟。被什么抓住，失去的东西就不一样。被楼上的人抓住时，手电筒会掉在被抓的地方，得回到那里去捡。

![还没收拾完的403室，可以躲进床底下](../Media/readme/game-bedroom-zh-Hans.webp)

[在走廊里被追赶的画面（GIF，3.6 MB）](../Media/readme/night-listener-chase.gif)

## 下载

目前发布的是 0.9.1 测试版。这是正式 1.0 之前的候选版本，想先在更多电脑上、请第一次玩的人试一试。

1. 下载 [Windows 版 ZIP](https://github.com/easygap/Missing-Floor/releases/download/v0.9.1/MissingFloor-0.9.1-Windows.zip)。
2. 全部解压后运行 `MissingFloor.exe`。同一文件夹里的 `Engine` 和 `IndieGame` 文件夹要放在一起才能启动。
3. 选择“开始游戏”。进度会自动保存，之后选“继续游戏”就能接着玩。

可执行文件没有签名，第一次运行时可能会弹出“Windows 已保护你的电脑”。点“更多信息”，再点“仍要运行”即可。

语言一开始跟随 Windows 的显示语言，可以在设置里修改。测试过的电脑配置和打不开时可以试的办法，写在[运行说明](../PLAYING.md)（韩文）里。

## 操作说明

| 按键 | 操作 |
|---|---|
| WASD、鼠标 | 移动、环顾 |
| 左 Shift、C、空格 | 奔跑、蹲下、跳跃 |
| E | 调查、开门、躲藏 |
| 长按 E | 悄悄开关门、贴墙倾听 |
| Q | 敲击 |
| 左 Ctrl | 屏住呼吸 |
| F | 手电筒 |
| F1 | 目标与操作 |
| Tab、H | 调查记录（白天）、提示 |
| ← →、鼠标滚轮 | 翻页 |
| Esc、F10 | 暂停、无障碍设置 |

也可以用手柄玩。按键和按钮可以在设置里重新分配。

## 难度与无障碍

如果被追让你吃不消，可以按 `F10` 把难度改成“不追击”。这样就不会被抓，只跟着剧情和谜题走。不管哪种难度，能看到的结局都一样。

字幕大小和背景、声音方向提示、画面晃动、灯光闪烁、镜头畸变都可以调整。需要一直按住的操作，可以改成按一下保持。麦克风输入默认关闭。

![无障碍设置画面](../Media/readme/settings-accessibility-20260929-zh-Hans.webp)

## 素材来源

音效来自 OpenGameArt 和 Kenney，地面和墙面纹理来自 ambientCG，实物扫描道具来自 Poly Haven，均为 CC0 素材。字体是 Pretendard 和 Gowun Batang（SIL Open Font License 1.1）。游戏里也有用 AI 做的东西。部分印刷品、标识和纹理是用 AI 生成后再手工修改的图片。楼上的人、黑影、管理员、303室的邻居等人物和怪异的身体，以及小巷里的猫和外卖摩托车，是先用 TRELLIS.2 把 AI 图片转成 3D，再在 Blender 里修整并加上骨骼。其他不少道具也是参照 AI 生成的草图在 Blender 里做的。详细来源和使用条件见[素材清单](../ASSET_POLICY.md)（韩文）。

## 问题反馈

如果游戏卡住或出现奇怪的情况，请在[问题反馈](https://github.com/easygap/Missing-Floor/issues/new?template=bug_report.yml)里写下是在哪个场景、发生了什么。也欢迎留下[试玩感想和建议](https://github.com/easygap/Missing-Floor/issues/new?template=feedback.yml)。

想从源码构建，请看[运行说明里的“소스에서 빌드하기”](../PLAYING.md#소스에서-빌드하기)（韩文）。需要 Unreal Engine 5.8、带“使用 C++ 的游戏开发”工作负载的 Visual Studio、Windows SDK 和 Git LFS。

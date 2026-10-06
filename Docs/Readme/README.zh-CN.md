[한국어](../../README.md) · [English](README.en.md) · [日本語](README.ja.md) · **简体中文** · [繁體中文](README.zh-TW.md)

# Missing Floor

一款以首尔老旧居民楼为背景的第一人称恐怖游戏，正在用 Unreal Engine 5 开发，可以免费下载 Windows 试玩版。

![雨后小巷尽头的四层公寓楼，只有两扇窗亮着灯](../Media/readme/title-menu-first-run-1080-zh-Hans.webp)

退回来的包裹上，哥哥的地址写着：月光公寓501室。
可这栋楼只有四层。

为了找哥哥，侑潭搬进了403室。搬来的第一晚，凌晨四点半，
上面明明没有楼层，天花板却传来三下敲击声。

[下载试玩版（Windows，0.2.4）](https://github.com/easygap/Missing-Floor/releases/download/v0.2.4/MissingFloor-0.2.4-Windows.zip) · [预告片](https://github.com/easygap/Missing-Floor/releases/download/v0.2.3/MissingFloor-Trailer.mp4) · [操作说明](#操作说明)

## 白天

线索只有退回的包裹和一条语音留言。401室的奶奶在这栋楼住了三十年，巷子对面便利店打工的店员对附近的传闻知道得不少。也许有人还记得哥哥。

<table>
  <tr>
    <td width="50%"><img src="../Media/readme/game-alley.webp" alt="公寓门前的小巷，尽头能看到便利店招牌"></td>
    <td width="50%"><img src="../Media/readme/game-store.webp" alt="黎明24便利店的收银台"></td>
  </tr>
  <tr><td>公寓门前的小巷</td><td>黎明24便利店</td></tr>
</table>

管理室的账本和监控记录对不上。数一数电表，把耳朵贴到墙上听听。看过的记录随时可以按`Tab`翻看，卡住了就按`H`看提示。

招牌和墙上的告示都是韩文，这里毕竟是首尔。需要读的文件，调查时会显示中文。

## 夜晚

每天凌晨四点半到五点半，这栋楼里有些东西是醒着的。

楼上的人看不见，只会循着声音过来。别跑，开门要慢。它从你身边经过时，记得屏住呼吸。

在黑暗里待久了，黑暗会聚拢起来。越盯着看它就越大，移开视线，往有灯的地方走。

就算门外传来熟人的声音，也别开门。

![手电筒照着的夜里的管理室桌面](../Media/readme/game-booth.webp)

可以躲进衣柜或床底下，也可以插上门闩、关上楼梯间的防火门。手电筒的电池会用完，记得去便利店买。

被抓住游戏也不会结束。你会在自己的房间里醒来，找到的线索都还在，只是时间不会倒流。

![黎明时分的403室](../Media/readme/game-bedroom-dawn.webp)

[走廊里被追赶的画面（GIF，4.8 MB）](../Media/readme/night-listener-chase.gif)

## 下载

目前提供的是试玩版 0.2.4。躲藏、门闩和防火门、手电筒电池，以及除了楼上的人以外、夜里会来的其他东西，会在下一个试玩版加入。二楼和三楼也会在那时开放。

1. 下载 [Windows 版 ZIP](https://github.com/easygap/Missing-Floor/releases/download/v0.2.4/MissingFloor-0.2.4-Windows.zip)。
2. 完整解压后运行`MissingFloor.exe`。同一文件夹里的`Engine`和`IndieGame`缺一不可。
3. 选择“开始游戏”。进度会自动保存，下次从“继续游戏”接着玩。

如果弹出“Windows 已保护你的电脑”，先点“更多信息”，再点“仍要运行”。这是因为可执行文件没有签名。

游戏支持简体中文、繁体中文、韩语、英语和日语。首次启动时跟随 Windows 的显示语言，之后可以在设置里切换。[运行指南](../PLAYING.md)目前只有韩文版，里面写了测试用的电脑配置和打不开时的处理办法。

## 操作说明

| 按键 | 操作 |
|---|---|
| WASD / 鼠标 | 移动 / 转动视角 |
| 左 Shift / C / 空格 | 奔跑 / 蹲下 / 跳跃 |
| E | 调查 / 开门 / 躲藏 |
| 长按 E | 轻轻开关门 / 贴墙倾听 |
| Q | 敲击 |
| 左 Ctrl | 屏住呼吸 |
| F | 手电筒 |
| F1 | 目标与操作 |
| Tab / H | 调查记录（白天）/ 提示 |
| ← → / 鼠标滚轮 | 翻页 |
| Esc / F10 | 暂停 / 辅助功能设置 |

也支持手柄。按键和按钮可以在设置里重新分配。

## 难度与辅助功能

如果怕被追，可以按`F10`把难度改成“不追击”。这样什么都抓不到你，故事和谜题照样能玩到最后。任何难度都能看到全部结局。

字幕的大小和背景、声音方向提示、画面晃动、光线闪烁和画面质感都可以调整。不方便长按的话，可以改成按一下就开始。麦克风输入是可选功能，默认关闭。

![辅助功能设置界面](../Media/readme/settings-accessibility-20260929-zh-Hans.webp)

## 素材来源

音效用 OpenGameArt 和 Kenney 的 CC0 素材包加工而成。照片纹理来自 ambientCG，扫描道具来自 Poly Haven，都是 CC0。字体是 Pretendard 和 Gowun Batang，均采用 SIL Open Font License 1.1。部分印刷品和参考图先用图像生成模型做出，再经过修改。每项素材的来源和许可列在[素材清单](../ASSET_POLICY.md)（韩文）里。

## 问题反馈

游戏卡死或出现奇怪的情况时，请在[错误报告](https://github.com/easygap/Missing-Floor/issues/new?template=bug_report.yml)里写下发生的场景和现象。也欢迎留下[试玩感想和建议](https://github.com/easygap/Missing-Floor/issues/new?template=feedback.yml)。游戏还在开发中，粗糙的地方不少。

想从源码构建，请看[运行指南里的“소스에서 빌드하기”](../PLAYING.md#소스에서-빌드하기)（韩文）。需要 Unreal Engine 5.8、Visual Studio 的“使用 C++ 的游戏开发”工作负载、Windows SDK 和 Git LFS。

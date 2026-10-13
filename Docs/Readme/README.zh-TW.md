[한국어](../../README.md) · [English](README.en.md) · [日本語](README.ja.md) · [简体中文](README.zh-CN.md) · **繁體中文**

# Missing Floor

哥哥最後留下的地址是月光公寓501室。可是那棟樓只有四層。

這是一款以首爾老公寓為舞台的第一人稱恐怖遊戲。你扮演為了尋找失蹤的哥哥而搬進403室的侑潭，白天向鄰居打聽、翻查樓裡的紀錄，天亮前則要避開從天花板上面下來的東西。遊戲以 Unreal Engine 5 開發，Windows 測試版可以免費下載。

![雨後小巷盡頭的四層公寓，只有兩扇窗亮著燈](../Media/readme/title-menu-first-run-1080-zh-Hant.webp)

目前能玩的是 0.9.1 測試版（Windows），支援繁體中文、簡體中文、韓文、英文和日文。

[下載測試版（Windows，0.9.1）](https://github.com/easygap/Missing-Floor/releases/download/v0.9.1/MissingFloor-0.9.1-Windows.zip) · [預告片（英文字幕）](https://github.com/easygap/Missing-Floor/releases/download/v0.9.0/MissingFloor-Trailer-en.mp4) · [操作說明](#操作說明)

## 白天

搬來的第一晚，凌晨四點半，天花板傳來三下敲擊聲。403室在頂樓。

天亮後，就沿著哥哥留下的痕跡在樓裡和小巷裡走動。401室的阿嬤在這棟樓住了三十年，在小巷對面便利商店上班的娜琳對街坊的事很清楚。

![403室所在的四樓走廊，每扇門上都貼著瓦斯檢查貼紙和沒撕掉的傳單](../Media/readme/game-corridor-day-zh-Hant.webp)

<table>
  <tr>
    <td width="50%"><img src="../Media/readme/game-alley.webp" alt="公寓門前的小巷，盡頭看得到便利商店的招牌"></td>
    <td width="50%"><img src="../Media/readme/game-store.webp" alt="黎明24便利商店的收銀台"></td>
  </tr>
  <tr><td>公寓門前的小巷</td><td>黎明24便利商店</td></tr>
</table>

管理室的帳本、監視器畫面和一樓電表上的紀錄，總有對不起來的地方。貼著牆聽到的聲音也是線索。讀過的紀錄按 `Tab` 可以再看，卡住時按 `H`，侑潭會自言自語地提醒該看哪裡。

場景裡的招牌和告示保留韓文，和劇情有關的文件調查時會顯示中文。

<table>
  <tr>
    <td width="50%"><img src="../Media/readme/p1-meter-cabinet-zh-Hant.webp" alt="各戶的電表和抄表單"></td>
    <td width="50%"><img src="../Media/readme/game-booth.webp" alt="擺著帳本和監視器螢幕的管理室桌子"></td>
  </tr>
  <tr><td>一樓電表箱</td><td>管理室</td></tr>
</table>

[在樓裡和附近走動的畫面（GIF，3.0 MB）](../Media/readme/readme-route-preview-zh-Hant.gif)

## 凌晨四點半

凌晨四點半到五點半，大門被鎖上，樓上的人在走廊裡爬來爬去。它看不見，卻聽得見。跑起來或用力關門，它就會往那邊過來；它從身邊經過時，只能憋住氣。

在沒開燈的地方待太久，會出現一個黑影，越盯著它看就越大。門外就算傳來熟人的聲音，也不能開門。

![熄了燈的走廊，鄰居家的燈光從門縫底下透出來](../Media/readme/game-corridor-night-zh-Hant.webp)

可以躲進衣櫃或床底下，也可以用玄關門閂和樓梯間的防火門擋住去路。手電筒的電池白天在便利商店買。

被抓到也不會從那一晚的開頭重來。醒過來時人在被拖走的地方，中間已經過了幾分鐘。被什麼抓到，失去的東西就不一樣。被樓上的人抓到時，手電筒會掉在被抓的地方，得回到那裡去撿。

![還沒整理完的403室，可以躲進床底下](../Media/readme/game-bedroom-zh-Hant.webp)

[在走廊裡被追趕的畫面（GIF，3.6 MB）](../Media/readme/night-listener-chase.gif)

## 下載

目前發布的是 0.9.1 測試版。這是正式 1.0 之前的候選版本，想先在更多電腦上、請第一次玩的人試試看。

1. 下載 [Windows 版 ZIP](https://github.com/easygap/Missing-Floor/releases/download/v0.9.1/MissingFloor-0.9.1-Windows.zip)。
2. 全部解壓縮後執行 `MissingFloor.exe`。同一資料夾裡的 `Engine` 和 `IndieGame` 資料夾要放在一起才能啟動。
3. 選擇「開始遊戲」。進度會自動儲存，之後選「繼續遊戲」就能接著玩。

執行檔沒有簽章，第一次執行時可能會跳出「Windows 已保護您的電腦」。按「其他資訊」，再按「仍要執行」即可。

語言一開始會跟隨 Windows 的顯示語言，可以在設定裡更改。測試過的電腦規格和打不開時可以試的方法，寫在[執行說明](../PLAYING.md)（韓文）裡。

## 操作說明

| 按鍵 | 操作 |
|---|---|
| WASD、滑鼠 | 移動、環顧 |
| 左 Shift、C、空白鍵 | 奔跑、蹲下、跳躍 |
| E | 調查、開門、躲藏 |
| 長按 E | 悄悄開關門、貼牆聆聽 |
| Q | 敲擊 |
| 左 Ctrl | 憋氣 |
| F | 手電筒 |
| F1 | 目標與操作 |
| Tab、H | 調查紀錄（白天）、提示 |
| ← →、滑鼠滾輪 | 翻頁 |
| Esc、F10 | 暫停、無障礙設定 |

也可以用手把玩。按鍵和按鈕可以在設定裡重新配置。

## 難度與無障礙

如果被追讓你吃不消，可以按 `F10` 把難度改成「不追擊」。這樣就不會被抓，只跟著劇情和謎題走。不管哪種難度，能看到的結局都一樣。

字幕大小和背景、聲音方向提示、畫面晃動、燈光閃爍、鏡頭變形都可以調整。需要一直按住的操作，可以改成按一下維持。麥克風輸入預設關閉。

![無障礙設定畫面](../Media/readme/settings-accessibility-20260929-zh-Hant.webp)

## 素材來源

音效來自 OpenGameArt 和 Kenney，地面和牆面材質來自 ambientCG，實物掃描道具來自 Poly Haven，皆為 CC0 素材。字型是 Pretendard 和 Gowun Batang（SIL Open Font License 1.1）。遊戲裡也有用 AI 做的東西。部分印刷品、標示和材質是用 AI 生成後再手工修改的圖片。樓上的人、黑影、管理員、303室的鄰居等人物和怪異的身體，還有小巷裡的貓和外送機車，是先用 TRELLIS.2 把 AI 圖片轉成 3D，再在 Blender 裡修整並加上骨架。其他不少道具也是參考 AI 生成的草圖，在 Blender 裡做出來的。詳細來源和使用條件見[素材清單](../ASSET_POLICY.md)（韓文）。

## 問題回報

如果遊戲卡住或出現奇怪的狀況，請在[問題回報](https://github.com/easygap/Missing-Floor/issues/new?template=bug_report.yml)寫下是在哪個場景、發生了什麼。也歡迎留下[試玩心得和建議](https://github.com/easygap/Missing-Floor/issues/new?template=feedback.yml)。

想從原始碼建置，請看[執行說明裡的「소스에서 빌드하기」](../PLAYING.md#소스에서-빌드하기)（韓文）。需要 Unreal Engine 5.8、含「使用 C++ 的遊戲開發」工作負載的 Visual Studio、Windows SDK 和 Git LFS。

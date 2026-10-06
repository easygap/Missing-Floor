[한국어](../../README.md) · [English](README.en.md) · [日本語](README.ja.md) · [简体中文](README.zh-CN.md) · **繁體中文**

# Missing Floor

以首爾老舊公寓為舞台的第一人稱恐怖遊戲，目前正以 Unreal Engine 5 開發，Windows 試玩版可免費下載。

![雨後巷子盡頭的四層樓公寓，只有兩扇窗亮著燈](../Media/readme/title-menu-first-run-1080-zh-Hant.webp)

退回的包裹上，哥哥的地址寫著月光公寓501室。
但這棟樓只有四層。

為了找哥哥搬進403室的第一晚，凌晨四點半，
上面明明沒有樓層，卻有人敲了天花板三下。

[下載試玩版（Windows，0.2.4）](https://github.com/easygap/Missing-Floor/releases/download/v0.2.4/MissingFloor-0.2.4-Windows.zip) · [預告片](https://github.com/easygap/Missing-Floor/releases/download/v0.2.3/MissingFloor-Trailer.mp4) · [操作方式](#操作方式)

## 白天

手上的線索只有退回的包裹和一則語音留言。401室的阿嬤在這棟樓住了三十年，巷子對面便利商店打工的店員對附近的八卦很清楚。說不定有人還記得哥哥。

<table>
  <tr>
    <td width="50%"><img src="../Media/readme/game-alley.webp" alt="公寓前的巷子，盡頭看得到便利商店的招牌"></td>
    <td width="50%"><img src="../Media/readme/game-store.webp" alt="黎明24便利商店的櫃檯"></td>
  </tr>
  <tr><td>公寓前的巷子</td><td>黎明24便利商店</td></tr>
</table>

管理室的帳本和監視器紀錄兜不起來。數數看電錶，把耳朵貼在牆上聽聽。看過的紀錄可以按`Tab`再翻出來，卡關時按`H`看提示。

招牌和牆上的告示都是韓文，畢竟是首爾的巷弄。需要讀的文件，調查時會顯示中文。

## 夜晚

每天凌晨四點半到五點半，這棟樓裡有些東西是醒著的。

樓上的人看不見，只會循著聲音過來。不要用跑的，門要慢慢開；它經過你身邊時，記得憋氣。

在暗處待太久，黑暗會聚攏起來。越盯著看它就長得越大，把視線移開，往有燈的地方走。

就算門外傳來熟人的聲音，也不要開門。

![手電筒照著的夜晚管理室桌面](../Media/readme/game-booth.webp)

可以躲進衣櫃或床底下，也可以插上門閂、把樓梯間的防火門關上。手電筒的電池會用完，記得去便利商店買。

被抓到，遊戲也不會結束。你會在自己房間醒來，找到的線索都還在，只是時間不會倒轉。

![天快亮時的403室](../Media/readme/game-bedroom-dawn.webp)

[在走廊被追的畫面（GIF，4.8 MB）](../Media/readme/night-listener-chase.gif)

## 下載

目前提供的是 0.2.4 試玩版。躲藏、門閂和防火門、手電筒電池，以及樓上的人之外、其他夜裡會來的東西，會在下一個試玩版加入。

1. 下載 [Windows 版 ZIP 檔](https://github.com/easygap/Missing-Floor/releases/download/v0.2.4/MissingFloor-0.2.4-Windows.zip)。
2. 完整解壓縮後執行`MissingFloor.exe`。同一個資料夾裡的`Engine`和`IndieGame`都要留著，不然開不起來。
3. 選擇「開始遊戲」。進度會自動儲存，下次可以從「繼續遊戲」接著玩。

如果跳出「Windows 已保護您的電腦」，請先按「其他資訊」，再按「仍要執行」。執行檔沒有簽章，才會出現這個畫面。

遊戲支援繁體中文、簡體中文、韓文、英文和日文。第一次啟動會使用 Windows 的顯示語言，之後可以在設定裡切換。[執行說明](../PLAYING.md)目前只有韓文版，裡面有測試過的電腦規格，以及開不起來時可以怎麼做。

## 操作方式

| 按鍵 | 操作 |
|---|---|
| WASD / 滑鼠 | 移動 / 轉動視角 |
| 左 Shift / C / 空白鍵 | 奔跑 / 蹲下 / 跳躍 |
| E | 調查 / 開門 / 躲藏 |
| 長按 E | 輕輕開關門 / 貼牆傾聽 |
| Q | 敲擊 |
| 左 Ctrl | 憋氣 |
| F | 手電筒 |
| F1 | 目標與操作 |
| Tab / H | 調查紀錄（白天）/ 提示 |
| ← → / 滑鼠滾輪 | 翻頁 |
| Esc / F10 | 暫停 / 無障礙設定 |

也支援手把。按鍵和按鈕都可以在設定裡重新指定。

## 難度與無障礙設定

怕被追的話，按`F10`把難度改成「不追擊」。這樣什麼都抓不到你，故事和謎題一樣能玩到最後。不管哪個難度，所有結局都看得到。

字幕大小與背景、聲音方向提示、畫面晃動、光線閃爍和畫面質感都可以調整。長按不方便的話，可以改成按一下就開始。麥克風輸入是選用功能，預設關閉。

![無障礙設定畫面](../Media/readme/settings-accessibility-20260929-zh-Hant.webp)

## 素材來源

音效是用 OpenGameArt 和 Kenney 的 CC0 素材包加工而成。照片材質來自 ambientCG，掃描道具來自 Poly Haven，全部是 CC0。字型是 Pretendard 和 Gowun Batang，都採用 SIL Open Font License 1.1。部分印刷品和參考圖是先用圖像生成模型做出來，再加以修改。每項素材的來源與授權都列在[素材清單](../ASSET_POLICY.md)（韓文）。

## 問題回報

遊戲當掉或出現怪怪的狀況時，請到[錯誤回報](https://github.com/easygap/Missing-Floor/issues/new?template=bug_report.yml)寫下發生的場景和情形。也歡迎留下[試玩心得與建議](https://github.com/easygap/Missing-Floor/issues/new?template=feedback.yml)。遊戲還在開發中，粗糙的地方不少。

想從原始碼建置的話，請看[執行說明的「소스에서 빌드하기」](../PLAYING.md#소스에서-빌드하기)（韓文）。需要 Unreal Engine 5.8、Visual Studio 的「使用 C++ 的遊戲開發」工作負載、Windows SDK 和 Git LFS。

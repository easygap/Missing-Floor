**한국어** · [English](Docs/Readme/README.en.md) · [日本語](Docs/Readme/README.ja.md) · [简体中文](Docs/Readme/README.zh-CN.md) · [繁體中文](Docs/Readme/README.zh-TW.md)

# Missing Floor

한국의 오래된 빌라를 배경으로 한 1인칭 공포 게임입니다. Unreal Engine 5로 만들고 있고, Windows용 무료 플레이 테스트를 받을 수 있습니다.

![비 그친 골목 끝의 4층짜리 빌라. 창 두 개에만 불이 켜져 있다.](Docs/Media/readme/title-menu-first-run-1080.webp)

반송된 택배에 적힌 오빠의 주소는 달빛빌라 501호.
그런데 이 빌라는 4층까지밖에 없다.

오빠를 찾으러 403호에 이사 온 날 밤, 새벽 네 시 반.
위층도 없는 천장에서 누가 세 번 두드린다.

[플레이 테스트 받기 (Windows, 0.2.4)](https://github.com/easygap/Missing-Floor/releases/download/v0.2.4/MissingFloor-0.2.4-Windows.zip) · [예고편](https://github.com/easygap/Missing-Floor/releases/download/v0.2.3/MissingFloor-Trailer.mp4) · [조작](#조작)

## 낮

오빠가 남긴 건 반송된 택배와 음성 메시지 하나뿐입니다. 401호 할머니는 이 건물에서 30년을 살았고, 골목 건너 편의점 알바생은 동네 소문을 꽤 압니다. 오빠를 기억하는 사람이 있을지도 모릅니다.

![403호가 있는 4층 복도. 현관문마다 가스 점검 스티커와 떼지 않은 전단지가 붙어 있다.](Docs/Media/readme/game-corridor-day.webp)

<table>
  <tr>
    <td width="50%"><img src="Docs/Media/readme/game-alley.webp" alt="빌라 앞 골목. 끝에 편의점 간판이 보인다."></td>
    <td width="50%"><img src="Docs/Media/readme/game-store.webp" alt="새벽24 편의점 계산대"></td>
  </tr>
  <tr><td>빌라 앞 골목</td><td>새벽24 편의점</td></tr>
</table>

관리실 장부와 CCTV 기록은 서로 맞지 않습니다. 계량기를 세어 보고 벽에 귀를 대 보세요. 읽은 기록은 `Tab`으로 다시 볼 수 있고, 막히면 `H`를 누르세요.

<table>
  <tr>
    <td width="50%"><img src="Docs/Media/readme/p1-meter-cabinet.webp" alt="호실별 계량기와 검침표"></td>
    <td width="50%"><img src="Docs/Media/readme/game-booth.webp" alt="장부와 CCTV 모니터가 놓인 관리실 책상"></td>
  </tr>
  <tr><td>1층 계량기함</td><td>관리실</td></tr>
</table>

[집과 동네를 걷는 장면 (GIF, 3.3 MB)](Docs/Media/readme/readme-route-preview.gif)

## 밤

새벽 네 시 반부터 한 시간 동안은 건물에 깨어 있는 것들이 있습니다.

위층 사람은 앞을 보지 못하고 소리를 따라옵니다. 뛰지 말고 문은 천천히 여세요. 가까이 지나갈 때는 숨을 참으세요.

불 꺼진 곳에 오래 서 있으면 어둠이 뭉칩니다. 쳐다볼수록 커지니까 눈을 돌리고 불 켜진 데로 가세요.

현관문 밖에서 아는 사람 목소리가 들려도 열어 주지 마세요.

![불 꺼진 복도. 문 아래로 이웃집 불빛이 새어 나온다.](Docs/Media/readme/game-corridor-night.webp)

장롱이나 침대 밑에 숨을 수 있고, 현관 걸쇠를 걸거나 계단실 방화문을 닫아 둘 수도 있습니다. 손전등 건전지는 닳으니 편의점에서 사 두세요.

잡혀도 게임이 끝나지는 않습니다. 방에서 다시 눈을 뜨고, 찾은 단서는 그대로지만 시간은 되돌아가지 않습니다.

![짐을 다 풀지 못한 403호. 침대 밑에 숨을 수 있다.](Docs/Media/readme/game-bedroom.webp)

[복도에서 쫓기는 장면 (GIF, 4.8 MB)](Docs/Media/readme/night-listener-chase.gif)

## 다운로드

지금 받을 수 있는 건 플레이 테스트 0.2.4입니다. 숨기, 걸쇠와 방화문, 손전등 건전지, 위층 사람 말고 밤에 오는 것들은 다음 플레이 테스트부터 들어갑니다. 2층과 3층도 그때 열립니다.

1. [Windows용 ZIP](https://github.com/easygap/Missing-Floor/releases/download/v0.2.4/MissingFloor-0.2.4-Windows.zip)을 받습니다.
2. 압축을 모두 풀고 `MissingFloor.exe`를 실행합니다. 같은 폴더의 `Engine`, `IndieGame` 폴더가 함께 있어야 켜집니다.
3. ‘게임 시작’을 누릅니다. 진행은 자동으로 저장됩니다. 다음부터는 ‘이어하기’를 고르세요.

처음 실행할 때 「Windows의 PC 보호」 창이 뜨면 ‘추가 정보’를 누른 뒤 ‘실행’을 고르세요. 실행 파일에 서명이 없어서 뜨는 창입니다.

게임은 한국어, 영어, 일본어, 중국어(간체·번체)를 지원합니다. 처음에는 Windows 언어를 따르고 설정에서 바꿀 수 있습니다. 확인한 PC 사양과 실행이 안 될 때 할 일은 [실행 안내](Docs/PLAYING.md)에 있습니다.

## 조작

| 키 | 동작 |
|---|---|
| `W A S D` · 마우스 | 이동 · 둘러보기 |
| `왼쪽 Shift` · `C` · `Space` | 달리기 · 앉기 · 점프 |
| `E` | 살펴보기 · 문 열기 · 숨기 |
| `E` 길게 | 문을 조용히 여닫기 · 벽에 귀 대기 |
| `Q` · `왼쪽 Ctrl` | 두드리기 · 숨 참기 |
| `F` | 손전등 |
| `F1` | 목표와 조작 |
| `Tab` · `H` | 기록 보기(낮) · 힌트 |
| `← →` · 마우스 휠 | 문서 넘기기 |
| `Esc` · `F10` | 일시정지 · 접근성 설정 |

게임패드도 됩니다. 키와 버튼은 설정에서 바꿀 수 있습니다.

## 난이도와 설정

쫓기는 게 부담스러우면 `F10`에서 난이도를 ‘추격 없음’으로 바꾸세요. 아무것에도 잡히지 않고 이야기와 퍼즐을 끝까지 진행할 수 있습니다. 어느 난이도에서도 결말은 모두 볼 수 있습니다.

자막 크기와 배경, 소리 방향 표시, 화면 흔들림과 빛 깜빡임, 화면 질감의 세기를 조절할 수 있습니다. 길게 누르기가 불편하면 한 번 눌러 시작하는 방식으로 바꿔 보세요. 마이크 입력은 쓰고 싶을 때만 켜면 되고, 처음에는 꺼져 있습니다.

![자막과 조작 방식을 바꾸는 접근성 설정](Docs/Media/readme/settings-accessibility-20260929.webp)

## 출처

효과음은 OpenGameArt와 Kenney의 CC0 팩을 가공해 썼습니다. 바닥과 벽의 사진 텍스처는 ambientCG, 실물 스캔 소품은 Poly Haven에서 받았고 모두 CC0입니다. 글꼴은 Pretendard와 고운바탕이고, 둘 다 SIL Open Font License 1.1입니다. 인쇄물과 참고 그림 일부는 이미지 생성 모델로 만든 뒤 다듬었습니다. 항목별 출처와 라이선스는 [에셋 대장](Docs/ASSET_POLICY.md)에 있습니다.

## 제보

게임이 멈추거나 이상하게 움직이면 [오류 제보](https://github.com/easygap/Missing-Floor/issues/new?template=bug_report.yml)에 장면과 증상을 남겨 주세요. [플레이 소감과 제안](https://github.com/easygap/Missing-Floor/issues/new?template=feedback.yml)도 받습니다. 아직 만드는 중이라 거친 데가 많습니다.

직접 빌드하려면 [실행 안내의 「소스에서 빌드하기」](Docs/PLAYING.md#소스에서-빌드하기)를 보세요.

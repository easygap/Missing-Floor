# 에셋 고도화 계약 — 아틀라스 · 리토폴로지 · LOD · 물리 배치

이 문서는 「없는 층」의 에셋이 **실제 게임에서 돌아가는 상태**를 무엇으로
정의하고, 그것을 어떤 자동 검사로 지키는지 적는다. 셋 다 사람이 눈으로
보고 넘어가면 반드시 새는 종류의 것이라 전부 스크립트가 판정한다.

---

## 1. 물리적 타당성 — `Scripts/audit_world_geometry.py`

건물·골목·편의점·옥상은 레벨 에디터가 아니라 C++ 안의 센티미터
좌표로 조립된다. 빠르지만 눈으로는 검증이 불가능하다. 선반이 벽 속으로
3cm 들어가도, 병이 상판 위 4cm에 떠 있어도 컴파일러도 엔진도 아무 말을
하지 않는다. 플레이어만 "저건 물건이 저렇게 있을 수 없는데"라고 느낀다.

검사기는 UE 없이 `IGPrologueWorldScene.cpp`의
배치 호출(`CreateBlock`·`AddStoreStockBlock`·`CreatePhysicsProp`)을 다시
평가해 상자를 복원한다. 범위 for, 카운트 for, 배열 상수,
로컬 람다, 삼항 연산자까지 해석해 현재 배치식의 **78.8%**를 복원한다.

| 코드 | 뜻 | 판정 |
|---|---|---|
| `FLOATING` | 아래에 받치는 것이 없고 옆·위로도 닿는 것이 없다 | 오류 |
| `EMBEDDED` | 구조물 안에 완전히 잠겨 어느 면도 표면에 닿지 않는다 | 오류 |
| `PENETRATING` | 시뮬레이션 바디가 정적 충돌 안에서 시작한다 | 오류 |
| `SUNK` | 딛고 선 수평 슬래브보다 밑동이 제 높이의 1/4 넘게 내려갔다 | 경고 |

`PENETRATING`은 눈에 보이는 겹침이 아니라 **첫 프레임의 튕김**이다. Chaos는
겹친 두 물체를 밀어내서 푸는데, 신발장 단 속에서 시작한 슬리퍼는 그 위에
놓이는 것이 아니라 레벨이 열리자마자 밖으로 튀어나온다.

`SUNK`이 말하는 "바닥"은 **가로세로 60cm 이상인 수평 슬래브**뿐이다. 연석·
천장 몰딩·계단 손잡이도 한 축이 얇지만 물건이 원래 물려 들어가는 것들이라
(난간 동자는 제 난간을 지나가고, 상자는 옆의 연석에 걸친다) 바닥으로 치지
않는다. 슬래브가 슬래브에 5cm 겹치는 것도 건물이 그렇게 지어지는 것이지
바닥을 뚫고 떨어진 물건이 아니므로, 판정 대상에서 슬래브 모양 프롭도 뺀다.

```
python3 Scripts/audit_world_geometry.py            # 사람이 읽는 보고
python3 Scripts/audit_world_geometry.py --json     # 기계가 읽는 보고
python3 Scripts/audit_world_geometry.py --check    # 오류가 있으면 1로 종료
python3 Scripts/audit_world_geometry.py --coverage # 복원 못 한 배치식 목록
```

`--check`는 `Validate-Project.ps1`이 호출한다.

**판정에서 빼야 할 배치**는 호출 위에 이유와 함께 주석으로 표시한다.
숨긴 충돌 프록시처럼 보이지 않는 것이 정상인 경우가 그렇다.

```cpp
// physics-audit: intentional hidden collision proxy inside the frame
CreateBlock(...);
```

검사기가 스스로 무시하는 것은 셋뿐이고, 전부 이유가 분명하다.

- **Movable 블록** — 떨어지는 영수증, 올라오는 수위처럼 비트가 옮기는
  것. 저작 좌표는 시작 자세이지 놓인 자리가 아니다.
- **저작 메시 오버라이드** — `FVector(100.0f)`는 크기가 아니라 그 에셋
  자신의 100% 배율이므로 상자 부피에 아무 의미가 없다.
- **평가 불가능한 회전** — 런타임 벡터에서 나온 `Rotation()` 같은 것.
  대각선 길이로 감싼 보수적 경계만 남겨 이웃의 접촉 판정에는 쓰고,
  자기 자신은 판정하지 않는다.

---

## 2. 텍스처 아틀라스 — `Scripts/build_texture_atlas.py`

호수판·고지문·제품 라벨·상가 간판·임대차계약서 25장은 전부 타일링하지 않고,
편의점과 4층 복도에서는 대부분이 한 화면에 같이 있다. 그런데 각각이 자기
텍스처·자기 머티리얼·자기 드로우콜을 들고 있었다.

- 계약: `Scripts/texture_atlas_contract.py` (Pillow도 `unreal`도 안 씀)
- 페이지: 최대 2048px, 엔트리마다 8px **가장자리 연장** 여백(정확 복제)
  페이지는 자기 내용이 필요한 만큼만 크다 — 최대 크기로 한 번 채운 뒤
  각 페이지를 담기는 가장 작은 2의 거듭제곱 상자로 다시 채운다. 페이지
  수는 1차 통과가 정하므로 축소는 메모리를 줄일 뿐 페이지를 늘리지 않는다.
- 산출물: `Content/SourceArt/Atlas/T_PrintAtlas0..2_D.png` + `print_atlas.json`
  원본 6.91Mpx → 페이지 3장. 2048×2048 두 장(93.5 / 58.7%)과 꼬리 페이지
  1024×1024 한 장(50.0%), 합쳐 9.44Mpx다. 넓이로는 두 장(8.39Mpx)이면
  되지만, 폭 1024인 메모 둘과 라벨 넷은 여백 때문에 한 줄에 둘씩 설 수
  없어 세로로만 쌓이고 그 높이가 한 페이지를 넘는다. 2026-10-07에 쓰는
  곳이 없는 21장(옛 엘리베이터 표시판, 과자 봉지, 전단, 공지 따위)을 빼기
  전에는 46장, 10.86Mpx를 2048 세 장(12.58Mpx)에 담았다.
  **페이지 PNG는 커밋하지 않는다** — 커밋한 원본에서 스크립트가 결정적으로
  다시 굽는 파생물이고, 레이아웃과 원본 SHA-256은 `print_atlas.json`이
  들고 있어 `--check`가 낡은 굽기를 잡는다. `.gitignore`에서 한 줄만 빼면
  LFS로 커밋하는 쪽으로 바꿀 수 있다.
- 임포트: `Scripts/import_texture_atlas.py` (Clamp, BC7, 밉 상한, 스트리밍 안 함)
- 머티리얼: `create_textured_materials.py`가 매니페스트를 읽어
  `UV0 × scale + bias`로 한 페이지를 샘플한다

패킹은 MaxRects(best-short-side-fit)이고 **입력 순서와 무관하게 같은
결과**를 낸다. 페이지 가장자리에는 여백을 두지 않는다 — 옆에 아무것도
없고 샘플러가 Clamp하기 때문이다. 엔트리끼리는 언제나 여백 두 칸 떨어진다.

```
python3 Scripts/build_texture_atlas.py             # 굽기
python3 Scripts/build_texture_atlas.py --check     # 최신인지만 확인
python3 Scripts/build_texture_atlas.py --preflight # 에디터가 임포트할 수 있나
python3 Scripts/build_texture_atlas.py --self-test # 패커 자체 검증
```

`--preflight`는 **에디터를 열기 전에** 그 실행이 될지를 답한다. Pillow,
LFS를 받았는지, 매니페스트가 쓸 만한지, 페이지가 실제로 구워져 있고
매니페스트가 적은 형상과 같은지, 그리고 굽기가 현재 원본과 맞는지를 본다.
아트 빌드의 아틀라스 단계는 커맨들릿에서 몇 분을 쓴 뒤에야 아틀라스에
닿으므로, 그 실패들을 셸에서 1초에 먼저 잡는 값이 있다.

**밉에 대해.** 8px 여백은 밉 3까지 이웃을 밖에 둔다(8→4→2→1). 그 아래는
반 텍셀이라 섞이기 시작하는데, 밉 3에서 2048 페이지는 256px이고 256px짜리
고지문은 32px이라 읽을 수 있는 것이 이미 없다. 언리얼에는 텍스처별로 밉
사슬을 일찍 끊는 방법이 없으므로 이건 강제하는 값이 아니라 **여백이
충분하다는 것을 말하는 수**다(`GUTTER_SAFE_MIP_LEVELS`).

**스트리밍에 대해.** 페이지는 스트리밍하지 않는다(`never_stream`). 텍스처
스트리머는 머티리얼이 UV를 엔트리 크기로 줄여 쓴다는 걸 모르고, 메시가 페이지
전체를 덮는다고 보고 필요한 밉을 몇 단계 낮게 잡는다. 같은 페이지의 다른
인쇄물이 가까이 있으면 우연히 맞는다. 2026-10-07에 아틀라스를 다시 묶었을 때
편의점 담배 판매 안내가 낮은 밉으로 뭉개져 찍혀서 알았다. 예전 배치에서는 같은
페이지에 진열대 음료 라벨이 같이 있어서 멀쩡해 보였을 뿐이고, 4층 복도의 402호
문패는 그때도 글자 없는 하얀 판처럼 찍혔다. 스트리밍을 끄자 둘 다 또렷해졌다.
세 장을 늘 올려 두는 값은 12 MB 남짓이고, `validate_baked_art_assets.py`가 이
설정을 확인한다.

`--self-test`는 합성 텍스처로 배치 겹침·여백·페이지 이탈·UV 왕복·순서
독립성·페이지 수 하한, 그리고 성긴 페이지가 실제로 줄어드는지까지
검사한다. 원본 아트는 Git LFS에 있으므로
LFS를 받지 않은 체크아웃에서도 이 검증만은 돌아간다. 원본이 포인터
파일이면 패커는 쓰레기 페이지를 굽지 않고 그렇다고 말하고 멈춘다.

**아틀라스는 전부 선택 사항이다.** 매니페스트가 없으면 머티리얼은 예전처럼
각자 텍스처를 쓴다. 보이는 것은 같고 드로우콜만 는다.

아틀라스에서 빼는 텍스처는 `ATLAS_EXCLUSIONS`에 이유와 함께 적는다
(U로 타일링하는 가격 띠, 1K 파사드 간판, 밉이 없는 UI 텍스처, 그리고
**HUD가 경로로 직접 부르는 세 장**).

마지막 셋 — `T_MeterBox_D`, `T_PaperClean_V2_D`, `T_PaperOld_V2_D` — 은
세계에 붙는 인쇄 머티리얼이기도 해서 아틀라스 후보로 보이지만, 넣으면 안
된다. 캔버스 드로우는 텍스처 전체를 그리므로 rect를 가리킬 UV 변환이
없고, 더 나쁘게는 `LoadObject`가 문자열로 부르는 참조를 쿠커가 보지
못한다. 머티리얼이 페이지를 읽는 순간 그 텍스처를 팩에 붙들어 둘 것이
아무것도 남지 않고, HUD는 **패키징한 빌드에서만** 빈 자리를 그린다.
에디터에서는 끝까지 멀쩡해 보인다. §4가 이걸 검사한다.

---

## 3. 리토폴로지와 LOD — `Scripts/mesh_lod_contract.py`

이전 계약은 두 가지가 비어 있었다. 절차 메시는 엔진 **LOD 그룹**만
요청하고 끝났고, "근접 조사용"으로 표시한 히어로 프롭 28종은 **아무것도
요청하지 않아** LOD가 하나뿐인 채로 나갔다. 방 건너편에서도 전밀도로
그려졌다는 뜻이다. 포토그래메트리 스캔 15종도 마찬가지로 스캔된 밀도
그대로였다.

이제 세 등급 모두 같은 계약을 받는다.

| 등급 | LOD0 예산 | LOD 체인 (비율 @ 화면 크기) | 라이트맵 |
|---|---|---|---|
| `hero` | 12,000 tri | 55% @ 0.28 · 25% @ 0.09 · 10% @ 0.025 | 64 |
| `prop` | 3,000 tri | 45% @ 0.20 · 18% @ 0.06 · 7% @ 0.015 | 32 |
| `large` | 9,000 tri | 60% @ 0.14 · 30% @ 0.045 · 12% @ 0.010 | 96 |

- **리토폴로지**는 스태틱 메시가 만들어지기 *전에* 다이내믹 메시에서
  일어난다. 그래야 충돌 헐과 LOD 체인이 둘 다 예산에 맞춘 지오메트리에서
  파생된다.
- **인쇄면 메시**(라벨 띠, 컵 슬리브, 포스트잇, 원장, 달력)는 단순화하지
  않는다. UV 이음매를 넘어 용접하면 인쇄물이 병을 돌아가며 뭉개진다.
- **라이트맵 UV**를 채널 1에 굽는다. 없으면 패커가 UV0로 폴백하는데,
  그 UV0에는 인쇄 아트가 올라가 있다.
- 스캔 프롭은 LOD0 자체를 예산 비율로 줄인 뒤 같은 곡선을 붙인다.

`validate_baked_art_assets.py`가 메시마다 LOD 개수, LOD0 삼각형 수,
라이트맵 채널 존재, 인쇄면 밀도 하한을 확인한다. LOD가 하나뿐인 메시는
이제 실패다.

---

## 4. 쿠킹에 실제로 들어가는 것 — `Scripts/check_cook_references.py`

아틀라스는 페이지가 생겼다고 이득이 아니다. **그 텍스처들이 쿠킹에서
빠져야** 이득이다. 그리고 쿠커는 C++를 읽지 않는다. 쿠커가 보는 것은
`DirectoriesToAlwaysCook`, 에셋 매니저의 주 에셋 규칙, 맵, 그리고 그
패키지들이 **직렬화한** 참조뿐이다.

그래서 두 가지가 조용히 어긋난다.

- `LoadObject<UTexture2D>(nullptr, TEXT("/Game/..."))` 로만 불리는 에셋은
  팩에 없다. 에디터는 프로젝트 전체가 디스크에 있으니 찾아내고, 패키징한
  빌드만 널을 받는다. HUD는 계속 그리고, 그림만 없다.
- 인쇄 텍스처는 머티리얼이 아직 자기 텍스처를 샘플하는 동안에는 전부
  그대로 실린다. 에디터가 머티리얼을 다시 굽기 전까지 아틀라스는 순수
  비용이다.

이 검사기가 쿠커와 같은 설정을 읽고 같은 그래프를 따라간 뒤 셋을 답한다.

- `ATLAS_CODE_LOAD` — 아틀라스에 넣은 텍스처를 C++도 경로로 부른다.
  넣으면 안 되는 것을 넣었다는 뜻이다(§2 마지막 문단).
- `CODE_ONLY` — 존재하고, 코드가 부르고, 쿠킹된 어떤 패키지도 참조하지
  않는다. 패키징하면 널이다.
- `PHOTO_SPLIT` — 코드는 `T_X_D`를 부르는데 머티리얼은 전부
  `T_Photo_X_D`를 그린다(또는 그 반대). 같은 표면이 두 모습이 된다.
- `ABSENT` — 코드가 부르는 경로에 에셋이 아예 없다. 아직 아트 런이 굽지
  않은 것일 수 있어 보고만 하고 실패시키지 않는다. 다만 **널을 받고
  그대로 쓰는 코드**와 **널을 확인하고 폴백하는 코드**는 전혀 다른
  물건이라서 갈라 찍는다. 로드 결과를 담은 포인터를 찾아 그 뒤에서
  그 포인터에 대한 널 검사가 있는지 보고, 있으면 `ABSENT_GUARDED`다.
  읽어내지 못한 로드는 전부 `ABSENT` 쪽에 남긴다 — 못 읽은 것을 안전
  하다고 말하면 이 보고가 안심시키는 장치가 된다. 지금 트리에 걸리는
  항목은 없다. 오래 걸려 있던 `M_CarbonPaper`(먹지)는 아직 굽지 않은
  것이 아니라 **구워 놓고 회수하지 못한** 것이었다. 타깃 아트 패스가
  미러에서 되가져오는 파일이 손으로 관리하는 목록이었고, 그 패스가 처음
  만든 에셋은 목록에 있을 수가 없었다. `ABSENT`를 「아트 런 대기」로만
  읽으면 이렇게 이미 끝난 작업을 몇 달 놓친다.

여기에 실패시키지 않는 보고가 둘 더 있다. 쌍의 **안 쓰는 반쪽**(쿠킹
안 되고, 참조도 없고, 코드도 안 부르는 것)은 세어서 이름만 찍고,
`UNUSED_CAPTURE`는 임포트 결과를 아무것도 안 그리는 **캡처 폴더**를
그 안에 든 스캔 이름과 함께 찍는다.

`CODE_ONLY`로 걸린 HUD 텍스처 12장(노크·포옹 프레임 각 4, 대화 필름
그레인, 보정 벽, 일지 종이, 브러시드 메탈)은 `DefaultGame.ini`의
`PrimaryAssetTypesToScan` 규칙 하나로 붙들어 뒀다. `SpecificAssets`가
언리얼이 주는 유일한 **에셋 단위** 쿠킹 규칙이다 — 디렉터리를 적으면
아틀라스가 빼낸 텍스처 251장이 통째로 돌아온다.

이 규칙은 처음에 `DefaultEngine.ini`에 적혀 있었다. `UAssetManagerSettings`는
`config=Game`이라 언리얼은 그 줄을 읽지 않았고, 0.2.4까지의 배포본에는 규칙이
붙들어야 할 텍스처 8장이 없었다. 아래 파서는 줄의 문법만 보고 파일 자리는 보지
않았다. 2026-10-01 배포본의 IoStore 목록을 대조하는 `check_package_contents.py`가
잡았고, 지금은 이 검사도 `DefaultEngine.ini`에 남은 규칙을 실패로 본다.

### 규칙은 훑지 않고 파싱한다 — `Scripts/ue_config.py`

정규식으로 `+PrimaryAssetTypesToScan=(...)`을 **찾는** 것은 충분하지만
그 줄이 맞다고 말하기엔 부족하고, 여기서는 그게 유난히 중요하다. 이
규칙 하나가 HUD 텍스처 12장을 팩에 붙드는 유일한 장치이고, 언리얼은
읽지 못한 규칙을 아무도 안 보는 로그 한 줄로 보고하며, 증상은 패키징한
빌드의 빈 드로우다. 그래서 `ue_config.py`가 `FStructProperty::ImportText`가
받는 문법을 그대로 파싱한다.

```
struct := '(' [ pair { ',' pair } ] ')'
pair   := identifier '=' value
array  := '(' [ value { ',' value } ] ')'
value  := struct | array | '"' ... '"' | bare | <빈 값>
```

구조체와 배열은 표기가 같고 첫 원소가 `identifier=`인지로 갈린다. 빈
값(`SpecificAssets=,`)이 빈 배열의 표기라서 그냥 거를 수도 없다.

파싱한 뒤 「맞아 보이는데 아무것도 안 붙드는」 경우를 전부 본다.

- 필드 아닌 필드 이름(`SpecficAssets=`) — 언리얼은 매핑 못 하는 것을
  버리고 로그만 남긴다
- `EPrimaryAssetCookRule`에 없는 `CookRule` 값(`AlwaysCok`)
- `Package.Object`의 두 쪽이 다른 경로 — 최상위 에셋은 이름이 하나다
- 트리에 없는 에셋
- 명명한 에셋이 아닌 `AssetBaseClass` — 애셋 매니저는 그 클래스의
  하위만 등록하므로 클래스가 틀리면 규칙이 빈손이다
- 디렉터리도 에셋도 없는 규칙
- 정수가 아닌 `Priority`/`ChunkId`, 불리언 아닌 플래그
- 같은 파일의 다른 규칙들과 필드 집합이 다른 규칙(차등 검사) — 내보낸
  것이 아니라 손으로 쓴 규칙이라는 신호이고, 그 차이가 보통 실수다

`--explain-rules`는 여기서 한 걸음 더 간다. **규칙을 없앤 상태로 도달성을
다시 계산**해서 에셋 하나하나에 대해 「이 규칙이 붙들고 있다」/「규칙
없어도 쿠킹된다(여기선 잉여)」/「쿠킹 안 된다」를 답한다. 지금 12/12가
`HELD BY THIS RULE`이다.

### 아틀라스 25장도 같은 방식으로 — `--simulate-rebuild`

머티리얼 재굽기 뒤의 상태는 에디터를 돌려야 관찰되지만, 재굽기가 바꾸는
것은 머티리얼마다 **간선 하나**다 — 확산 샘플이 개별 텍스처에서 페이지로
옮겨간다. 그래서 그 편집을 그래프에 직접 하고 도달성을 다시 계산한다.
추측이 아니라 재굽기가 하는 일 그대로다.

중요한 출력은 「텍스처가 빠진다」가 아니라 **그 외에 무엇이 빠지고 무엇이
안 빠지는가**다. 다섯 가지를 본다.

- 메시 슬롯 등 **머티리얼이 아닌 참조자** — 재굽기가 건드리지 않으므로
  그 텍스처는 계속 실린다. 시뮬레이션도 그 간선을 건드리지 않는다
  (`/Game/Prototype/Materials` 밖의 참조자는 `stragglers`로 보고)
- 쿠킹 **규칙이나 AlwaysCook 디렉터리가 덮는** 텍스처 — 페이지 값만 내고
  절감은 없다
- `T_Photo_` **쌍둥이** — `_load_texture`가 사진 캡처를 먼저 고르므로,
  절차 원본으로 구운 페이지는 화면에 다른 그림을 올리고 쿠킹에 남는 것도
  계약한 쪽이 아니다
- **살아남은 재굽기 이전 샘플러** (아래)
- 그 텍스처를 통해서만 닿던 패키지 — 재굽기가 만드는 **부수 피해**

현재 결과: 재굽기가 끝나 **25장 모두 이미 빠져 있고**, 그 외에 빠지는
것도, 규칙이나 디렉터리가 붙드는 것도 없다.

### `update_in_place`가 남기던 참조

`create_flat_texture_materials(..., update_in_place=True)`는 옛 표현식을
지우지 않고 새 그래프를 **덧붙인** 뒤 출력만 다시 연결한다(프롤로그 CDO가
잡고 있는 머티리얼에서 표현식을 지우면 에디터가 죽을 수 있어서 그렇게
돼 있다). 컴파일된 셰이더에서는 끊긴 노드가 정리되니 아틀라스 전까지는
무해했다.

그런데 **끊긴 `UMaterialExpressionTextureSample`도 `UTexture2D` 하드
포인터를 그대로 들고 있고, 그 포인터는 직렬화된다.** 그래서 표적 패스가
마지막으로 만진 머티리얼은 페이지를 그리면서 개별 텍스처를 쿠킹에 남긴다.
해당하는 모드는 둘이고 25장 중 8장을 건드린다.

| 모드 | 머티리얼 | 아틀라스 텍스처 |
|---|---|---|
| `IG_PROP_RESPONSE_ONLY` | 종이 2 + 라벨 6 | 4 |
| `IG_CORRIDOR_SIGNAGE_ONLY` | 메모 2 + AUX 라벨 + 호수판 1 | 4 |

`_retire_pre_atlas_samples()`가 아틀라스 경로를 타는 머티리얼에서 남은
계약 텍스처 샘플러를 찾아 **그 `texture`를 페이지로 돌려놓는다.** 표현식을
지우지 않으므로 CDO 문제도 없고, 죽은 노드는 그대로 끊긴 채 남되 더는
텍스처를 지목하지 않는다. 동반 노멀/러프니스 맵은 계약 목록에 없으므로
건드리지 않는다.

사진 캡처가 있는 텍스처는 남은 샘플러가 `T_Photo_X_D`를 들고 있으므로
계약 이름만 맞춰서는 놓친다. `_is_pre_atlas_texture()`가 두 이름을 다 본다.
이건 조용한 실패다 — 계약 텍스처 `T_X_D`는 애초에 아무도 참조하지 않으니
「빠졌다」로 보이고, 실제로 팩에 남는 것은 캡처 쪽이다.

이건 UE 없이는 실행할 수 없으므로 **정적 감시도 같이 둔다.** 페이지와 그
페이지가 대체한 텍스처를 **동시에** 참조하는 패키지는 `--check`가
실패시킨다 — 어느 패스가 그 머티리얼을 썼든 걸린다.

### 표적 패스를 양쪽으로 돌려본다 — `--simulate-targeted`

어느 패스가 제자리 갱신인지, 그 패스가 어느 머티리얼을 건드리는지는
**`create_textured_materials.py`를 `ast`로 파싱해서** 읽는다
(`Scripts/material_passes.py`). 손으로 적은 목록은 모드가 하나 늘면 그날로
틀리기 때문이다. 파서는 세 가지 표기를 읽는다: 테이블 이름 그대로,
`{'M_X': TABLE['M_X']}` 딕셔너리 리터럴, 그리고 몇 줄 위에 선언한 튜플을
도는 `{name: TABLE[name] for name in names}` 컴프리헨션.

읽어낸 패스 19개 중 제자리 갱신이면서 아틀라스를 아는 것은 셋이고, 그중
계약 텍스처를 실제로 건드리는 것은 둘이다(`IG_RETAIL_SIGNS_ONLY`는
`SIGN_MATERIALS`뿐이라 0장).

그 둘을 **재굽기 이전 그래프 위에서** — 지금 이 저장소의 상태 그대로 —
**세 가지**로 돌린다. 두 가지가 아니라 셋인 이유는 회수가 실패하는 방식이
둘이기 때문이다: 아예 안 하거나, 계약 이름만 알고 하거나.

```
IG_PROP_RESPONSE_ONLY      4장   retired: drops  by name only: drops  not retired: stays
IG_CORRIDOR_SIGNAGE_ONLY   4장   retired: drops  by name only: drops  not retired: stays

PASS all 8 texture(s) leave the cook when a targeted pass retires the
     pre-atlas sampler, and stay when it does not
```

**셋을 나란히 보는 것이 요점이다.** 회수를 넣으면 8/8이 빠지고, 빼면
8/8이 남으며 머티리얼 8개가 페이지와 텍스처를 동시에 지목한다. 뒤쪽이
없으면 앞쪽은 장식이다.

가운데 열은 지금 25장에는 캡처가 하나도 없어서 왼쪽과 같지만, 하나라도
생기면 갈라진다. `--self-test`의 합성 프로젝트가 그 갈라짐을 붙들고 있다.

```
  캡처 없음   samples T_Print_D         retired: drops  by name only: drops  not retired: stays
  캡처 있음   samples T_Photo_Print_D   retired: drops  by name only: STAYS  not retired: stays
```

시뮬레이션도 `_load_texture`와 같은 규칙으로 **실제로 존재하는 간선**을
찾는다(`resolved_print_texture`). 없는 간선을 옮기면 아무것도 안 옮기고
아무것도 안 빠지면서 「절감했다」고 보고하게 되는데, 그게 정확히 캡처가
있을 때 벌어질 일이었다. `--simulate-rebuild`도 같은 해석을 쓰고,
`atlas_stale_samples`는 두 이름을 다 센다.

캡처가 있다는 것 자체는 **여전히 결함으로 보고한다.** 쿠킹은 이제 맞게
모델링되지만, 패커는 `Content/SourceArt/<stem>.png`를 굽는데 머티리얼은
캡처를 보고 있었으므로 **페이지에 다른 그림이 들어간다.**

### 캡처 쌍이 갈라진 곳 — `PHOTO_SPLIT`

`_load_texture`가 캡처를 먼저 고르는 규칙은 **머티리얼에만** 적용된다.
`LoadObject` 경로는 파일 하나를 지목할 뿐이라 그런 판단을 하지 않는다.
둘이 어긋나면 같은 표면이 **두 가지 모습**을 갖는다 — 월드는 이쪽,
HUD는 저쪽 — 그리고 아무것도 실패하지 않으므로 보지 않으면 모른다.

캡처가 있는 표면 14종 42장을 전부 훑어 이 모양을 하나 찾았다.

| | 머티리얼 참조 | 코드 경로 |
|---|---|---|
| `T_Photo_MetalBrushed_D` | **6** (거울·주방·금속·선반·스테인리스·현관문) | — |
| `T_MetalBrushed_D` | 0 | `IGHorrorHUD.cpp` |

일지 썸네일은 증거가 나온 **장소**를 대신한다(5층 계단참, 1층 배전반,
관리실 휴대폰이 전부 `Metal`이다). 그러니 플레이어가 본 금속이어야 하는데,
게임 어디에도 없는 텍스처를 그리고 있었다. 나머지 셋
(`Meter`·`Plaster`·`Tank`)은 전부 월드와 같은 텍스처를 쓴다 — 이것만
어긋나 있었다.

HUD를 캡처 쪽으로 돌리고 `SpecificAssets`에서 `T_MetalBrushed_D`를 뺐다
(캡처는 머티리얼 6개가 이미 쿠킹에 넣으므로 규칙이 필요 없다). 규칙은
12장에서 11장이 됐고 전부 여전히 `HELD BY THIS RULE`이다.

`--check`가 이 모양을 막는다. 되돌려 보면 정확히 이렇게 걸린다.

```
[PHOTO_SPLIT] T_MetalBrushed_D
    loaded by Source/IndieGame/Player/IGHorrorHUD.cpp
    but every material draws T_Photo_MetalBrushed_D (6 referrer(s));
    the same surface has two appearances
```

### 아무도 안 읽는 쌍의 반쪽 27장

쿠킹 문제는 아니다 — 이미 안 실린다. 보고하는 이유는 **함정**이라서다.
다음에 어떤 머티리얼이나 경로가 안 쓰는 쪽 이름에 닿으면, 아무도 일 년째
안 본 반쪽을 조용히 집어 든다.

`T_Asphalt_*`, `T_Blanket_*`, `T_Brick_*`, `T_CeilingTile_*`,
`T_Concrete_*`, `T_Jangpan_*`, `T_MetalBrushed_*`, `T_StoreTile_*`,
`T_WoodDark_*` — 캡처가 이겨서 남은 절차 원본 25장. 나머지 둘은 캡처인데
아무것도 못 이긴 쪽이고, 그건 아래 이야기다.

### 벽지 캡처는 벽지가 아니다 — `UNUSED_CAPTURE`

`Content/SourceArt/Photo/Wallpaper/`에 들어 있는 것은 AmbientCG
**`Plaster003`** — 회벽 스캔이다. 파일명이 아니라 **LFS에서 받아 눈으로
본 것**이다: 흙손 자국이 남은 흰빛 회벽 2048², 무늬도 이음매도 종이결도
없다. `Photo/Stucco/`의 `Plaster004`와 같은 재질을 색조만 달리한 것이라
사실상 중복이기도 하다. 그런데 `import_photo_textures.py`는 **폴더
이름으로** 에셋 이름을 짓는다.

```python
plan.append((os.path.join(surface_dir, entry), f"T_Photo_{surface}_{role}"))
```

그래서 회벽 스캔이 `T_Photo_Wallpaper_{D,N}`이 됐다. 폴더 이름이 그
캡처가 무엇인지 말하는 **유일한** 근거인데 그 이름이 내용과 다르다.

정작 아파트 벽은 `ApartmentWallpaperV2`를 쓴다 — `Prepare-AIArt.ps1:282`의
ImageGen `TextureApartmentWallpaperVintage`. 이쪽도 열어 보면 **진짜
벽지**다: 크림색 리넨 바탕에 잔잎·덩굴 무늬가 흩뿌려진, 90~2000년대 한국
빌라 실내 그대로. 회벽 스캔으로 대신할 수 있는 그림이 아니다.

벽면 네 종류가 이미 전부 임자가 있다는 것도 확인했다.

| 표면 | 아트 | 머티리얼 |
|---|---|---|
| 복도 회벽 `Stucco` | 캡처 `Plaster004` | `M_Stucco_X/_Y/Ceil` (3) |
| 외벽 `KoreanVillaStucco` | ImageGen | `M_VillaStucco_X/_Y` (2) |
| 5층 마른 석고 `MissingFloorDryPlaster` | ImageGen | 4 |
| 세대 벽지 `ApartmentWallpaperV2` | ImageGen | `M_Wallpaper_X/_Y/Ceil` (3) |

`Plaster003`이 들어갈 자리가 없다.

히스토리를 봐도 **한 번도 연결된 적이 없다.** 전체 이력에서
`"tex": "Wallpaper"` 를 찾으면 아무것도 안 나오고, 캡처 폴더와
`T_ApartmentWallpaperV2` 아트가 같은 커밋(`20025a4`)에 함께 들어왔다.
나중에 밀려난 것이 아니라 처음부터 배선된 적이 없다.

같은 이름으로 죽어 있는 것이 하나 더 있다.
`generate_surface_textures.py`의 `SURFACES["Wallpaper"] = build_wallpaper`가
매 빌드 `T_Wallpaper_D/N`을 굽는데(「pale weave wallpaper」), 이것도
소비자가 없다. 열어 보면 **256×256**에 균일한 회녹색과 흐릿한 얼룩뿐 —
프로젝트의 다른 표면이 전부 1K~2K인 것과 비교하면 그레이박스 시절의
자리표시자이지 대안이 아니다. 절차 원본과 캡처 둘 다 살아 있는 스펙
이름이 없는 채로 계속 만들어지고 있다.

검사기가 이걸 잡는다. 캡처 폴더 14개 중 임포트 결과를 아무 머티리얼도
안 그리는 것을 찾아, **폴더 안에 실제로 무엇이 있는지까지** 찍는다.

```
[UNUSED_CAPTURE] SourceArt/Photo/Wallpaper/ holds Plaster003
    imported as T_Photo_Wallpaper_D every art build; no material samples it
```

실패시키지는 않는다 — 빌드가 깨진 것이 아니라 죽은 아트다. 무엇을 지울지는
결정할 일이라 폴더도 `build_wallpaper`도 손대지 않았다.

다만 이 쌍이 증명하는 것은 **회수가 결정적이라는 것**이지 회수가 실제로
동작한다는 것이 아니다. 언리얼이 프로퍼티를 설정하는 것을 정적 검사가 볼
수는 없다. 대신 `retired: STAYS`가 뜨면 그건 유용한 실패다 — 그 패스가
아닌 **다른 무언가**가 텍스처를 붙들고 있다는 뜻이니까.

```
python3 Scripts/check_cook_references.py                          # 보고
python3 Scripts/check_cook_references.py --check                  # 걸리면 1
python3 Scripts/check_cook_references.py --check \
        --require-atlas-dropped                                   # 머티리얼 재굽기 후
python3 Scripts/check_cook_references.py --explain-rules           # 규칙별 에셋 판정
python3 Scripts/check_cook_references.py --simulate-rebuild        # 재굽기 후의 델타
python3 Scripts/check_cook_references.py --simulate-targeted       # 표적 패스 양쪽
python3 Scripts/check_cook_references.py --self-test              # 검사기 자체 검증
python3 Scripts/check_cook_references.py --json
```

`--self-test`는 합성 프로젝트(맵 하나, 메시 하나, 머티리얼 하나, 텍스처 셋,
HUD 소스 하나)를 임시 디렉터리에 짓고 확인한다. 머티리얼 재굽기 **전후**로
아틀라스 텍스처가 쿠킹에 남고 빠지는 것, `SpecificAssets`가 코드 전용
에셋을 붙들고 그것이 **유일한** 장치인 것, 페이지에 올린 텍스처를 코드가
부르는 충돌을 잡는 것, LFS를 받지 않은 체크아웃에서 실패가 아니라
**SKIP**을 내는 것, 그리고 위에 적은 **규칙이 망가지는 아홉 가지**를
전부 잡는 것. SKIP이 특히 중요하다 — 참조 그래프에 구멍이 있으면
「닿지 않는다」도 「닿는다」도 판정이 아니다.

돌연변이 서른하나를 넣어 전부 잡히는 것까지 확인했다 — 문자열 연결 복원,
AlwaysCook 파싱, LFS 포인터 감지, 경로 형태, CookRule 열거, 필드 집합,
구조체 파서, 필드·에셋 검증, 맵 시딩, 추이 폐쇄, 블라인드 스폿, 패스
목록 파싱, 제자리 갱신 플래그, 스펙 테이블, 회수 무력화, 그리고 캡처
관련 일곱(`photo_twin`, `resolved_print_texture`, `replaced_textures`,
`photo_aware` 플래그, `_capture_pair`, `photo_split_loads`,
`orphan_capture_pairs`), 그리고 `unused_capture_folders` 셋(무시하기,
전부 보고하기, 폴더 내용 안 찍기).

`--require-atlas-dropped`는 아직 아틀라스 텍스처가 쿠킹에 남아 있으면
실패한다. 커밋된 상태에서는 **당연히 실패한다** — 머티리얼이 아직 다시
구워지지 않았기 때문이다. 그래서 `Validate-Project.ps1`은 이 옵션 없이
돌리고, `Run-PrintAtlas.ps1`과 `Build-ArtAssets.ps1`은 머티리얼 단계
**뒤에** 이 옵션을 붙여 돌린다.

두 가지 한계를 분명히 해 둔다. 패키지 안의 `/Game/...` 문자열을 훑는
것이지 언리얼 패키지 형식을 파싱하는 것이 아니다 — 참조 집합을 **넓게**
잡으므로 「닿지 않는다」는 판정은 확실하고 아틀라스 절감은 과소평가된다.
그리고 이건 쿠커가 실제로 무엇을 했는지가 아니라 무엇을 볼 수 있는지를
읽는다. 애초에 없는 참조는 잡지만, 쿠커가 제 사정으로 버린 것은 못 잡는다.

---

## 실행 순서

```
Scripts/Build-ArtAssets.ps1
  Prepare-AIArt.ps1
  condition_ai_tiles.py
  generate_ai_pbr_maps.py
  build_texture_atlas.py        ← 아틀라스 굽기 (에디터 밖)
  --- 에디터 ---
  generate_meshes.py            ← 리토폴로지 + LOD 체인
  generate_surface_textures.py
  import_texture_atlas.py       ← 페이지 임포트
  create_textured_materials.py  ← 아틀라스를 읽는 머티리얼
  apply_photo_prop_lods.py      ← 스캔 프롭 예산 + 체인
  validate_baked_art_assets.py  ← 전부 확인
  --- 에디터 밖 ---
  check_cook_references.py --check --require-atlas-dropped
                                ← 아틀라스로 옮긴 텍스처가 정말 빠졌나

Scripts/Validate-Project.ps1
  audit_world_geometry.py --check
  build_texture_atlas.py --self-test
  check_cook_references.py --self-test
  check_cook_references.py --check
  check_cook_references.py --simulate-rebuild
  check_cook_references.py --simulate-targeted
```

아틀라스만 다시 돌릴 때는 아트 빌드 전체를 돌릴 이유가 없다.

```
Scripts/Run-PrintAtlas.ps1 -WhatIf   # 굽고 프리플라이트까지, 에디터는 안 엶
Scripts/Run-PrintAtlas.ps1           # 굽기 → 임포트 → 머티리얼 → 검증 → 쿠킹 참조
Scripts/Run-PrintAtlas.ps1 -SkipPack # 이미 구운 페이지를 쓴다
```

`-SkipPack`도 프리플라이트를 지나야 하므로 낡은 아틀라스를 실어 보낼 수는
없다. 임포트 단계가 `pages=0`으로 통과하는 경우(매니페스트를 못 찾음)는
아트 빌드에서는 정상이지만 이 스크립트에서는 실패로 잡는다.

머티리얼 단계는 `IG_PRINT_ATLAS_ONLY`로 돌아서 페이지를 읽는 인쇄물
머티리얼만 전체 빌드와 같은 방식으로 새로 만든다. 나머지 머티리얼은 다시
저장하지 않으므로 커밋에는 페이지 셋과 매니페스트, 인쇄물 머티리얼만 남는다.
페이지 수가 줄면 남는 `T_PrintAtlas<n>_D` 에셋과 PNG는 손으로 지운다.

## 아직 안 된 것

- `_retire_pre_atlas_samples()`는 **UE에서 돌려본 적이 없다.**
  `update_in_place` 경로에서 끊긴 샘플러가 텍스처를 붙들고 있었다는 사실,
  그 모드가 25장 중 8장을 건드린다는 사실, 회수를 빼면 그 8장이 전부
  쿠킹에 남는다는 사실, 그리고 계약 이름만 아는 회수는 캡처가 있는
  텍스처에서 아무 일도 하지 않는다는 사실은 확인했다
  (`--simulate-targeted` 세 열, 파스는 빌더 소스에서 `ast`로 읽는다).
  확인하지 **못한** 것은 언리얼이 실제로 그 프로퍼티를 설정하는지다 —
  정적 검사로는 볼 수 없다. 그쪽은 에디터가 한 번 돌아간 뒤 `--check`의
  정적 감시(페이지와 옛 텍스처를 동시에 참조하는 패키지)가 답한다.
- 지금 계약 25장에는 `T_Photo_` 캡처가 **하나도 없다.** 아틀라스 쪽 캡처
  경로는 합성 프로젝트로만 검증했고 실제 에셋으로 밟아 본 적은 없다.
  (표면 텍스처 쪽 캡처 42장은 실물로 훑었고 `PHOTO_SPLIT` 하나를 찾았다.)
- 일지 금속 썸네일을 캡처로 돌린 것은 **화면으로 확인하지 않았다.**
  참조 그래프상 월드의 금속 여섯 면과 같은 텍스처가 된다는 것까지는
  확실하지만, 캡처가 작은 썸네일에서 절차 원본보다 잘 읽히는지는 플레이로
  볼 일이다.
- 쌍의 안 쓰는 반쪽 27장은 **지우지 않았다.** 에디터 작업이고, 무엇을
  버릴지는 결정할 일이다. 쿠킹에는 이미 안 들어간다.
- `Photo/Wallpaper/`(내용은 `Plaster003`)와
  `generate_surface_textures.py`의 `build_wallpaper`도 **손대지 않았다.**
  둘 다 소비자가 없다는 것은 확인했지만, 회벽 스캔 하나를 버릴지
  이름을 고쳐 살릴지는 아트 쪽 결정이다. 지금은 매 아트 빌드가 쓰지 않을
  텍스처 다섯 장을 만든다(캡처 `_D`/`_N`, 절차 `_D`/`_N`, 그리고
  임포트된 `T_Photo_Wallpaper_D`).
- `IGHudTexture` 규칙은 **언리얼로 파싱해 본 것이 아니다.** 대신 언리얼의
  `ImportText` 문법을 구현해(§4의 `ue_config.py`) 파싱했고, 12장 전부
  경로가 트리의 패키지로 풀리고 `Package.Object` 두 쪽이 일치하고
  `Texture2D`로 보이며, 규칙을 없애면 전부 쿠킹에서 빠지는 것까지
  확인했다(`--explain-rules`: 12/12 `HELD BY THIS RULE`). 필드 집합은
  같은 파일의 기존 규칙 두 개와 동일하다. 그래도 최종 확인은 실제 쿠킹
  로그이고, 이 컨테이너에는 UE가 없다.
- 리토폴로지·LOD 계약의 실제 삼각형 수치는 UE 5.8에서
  `Build-ArtAssets.ps1`을 돌려야 확정된다. 예산 자체는 코드에 있고
  검사기가 강제하지만, 어느 메시가 실제로 얼마나 줄었는지는 그 실행의
  로그가 증거다.
- 정적 물리 검사의 복원율 78.8%. 나머지는 런타임 상태에 의존하는
  배치식이다(엔진 벡터에서 온 회전, 컴포넌트 경계에서 계산한 위치).
  `--coverage`가 그 목록을 그대로 찍는다.
- 4층 하강 계단을 계단실 안(X -330 서쪽)으로 옮기면서 밤1 「관망 주머니」가
  반층 참(3.5F) 바닥에서 계단 위로 바뀌었다. 야간 포털을 X -440에서 -452로
  밀어 두 단까지 열어 뒀다 — 포털 상자 반폭 24cm에 폰 캡슐 34cm이므로
  설 수 있는 서쪽 한계는 포털 중심에서 동쪽으로 58cm, 즉 X -394이고
  세 번째 디딤판(중심 -385, 두 단 아래)까지 들어간다. 형체는 계단실 서벽
  (X -445)에 귀를 대고 서 있고 두 값 모두에서 포털 상자 안이라 걸어서는
  닿을 수 없다. 실제 체감은 플레이로 확인해야 한다.

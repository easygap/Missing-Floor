# Blender · TRELLIS.2 에셋 파이프라인

2026-09-04부터 소품·설비·인물은 지오메트리 스크립트 상자 조립 대신 Blender에서
만든다. 두 갈래다.

- **딱딱한 것**(문·가구·설비·소품): `Scripts/blender/build_*.py`가 Blender
  헤드리스에서 절차 모델링하고 Cycles로 PBR을 굽는다.
- **살아 있는 것**(위층 사람·고양이·인물·유해): 기준 시트 한 칸을 ComfyUI
  네이티브 TRELLIS.2에 넣어 형상을 뽑고, `refine_generated.py`가 게임 메시로
  다듬는다.

둘 다 `Content/SourceArt/Blender/<SM_Name>/`에 FBX·텍스처·미리보기·manifest를
남기고, `Import-BlenderAssets.ps1`이 UE 에셋으로 만들어 `Content/`에 넣는다.

## 왜 에디터가 아닌가

2026-08-25부터 Smart App Control이 이 저장소의 `UnrealEditor-IndieGame.dll`을
막는다. 게임 모듈을 싣는 실행은 전부 안 된다. 그런데 메시·텍스처·재질
에셋은 게임 클래스를 참조하지 않는다. 게임 모듈이 없는 콘텐츠 전용 프로젝트
`%LOCALAPPDATA%\IndieGame\ArtImport`는 22초에 부팅하고, 거기서 만든 uasset을
`Content/`에 복사하면 그대로 쓰인다. `Import-BlenderAssets.ps1`이 이 흐름을
전부 자동으로 한다.

2026-09-08 오후에 Smart App Control을 껐고 그때부터 `-game` 캡처
(`Run-ReadmeCapture.ps1`, `Run-MissingFloor-NightCapture.bat`)가 다시 돈다.
반입은 그래도 이 흐름을 쓴다 — 게임 모듈을 다시 빌드하지 않아도 되고 한글
경로와도 무관하다. 다만 반입은 `-nullrhi`라 재질 컴파일 실패를 못 본다. 첫
캡처에서 마스터 재질 `M_IGBakedProp`이 Normal·ORM 기본 텍스처가 없어 컴파일에
실패했고 새 메시 전부가 기본 회색으로 찍혔다. 캡처 뒤에는 로그에서
`Failed to compile Material`을 꼭 찾아봐라.

## 도구 위치

| 도구 | 위치 | 비고 |
|---|---|---|
| Blender 5.2.0 LTS | `%LOCALAPPDATA%\Programs\Blender-5.2\Blender Foundation\Blender 5.2\blender.exe` | Downloads의 서명된 MSI를 `msiexec /a`로 푼 포터블. 관리자 권한 불필요. `IG_BLENDER`로 덮어쓸 수 있다 |
| ComfyUI 0.34.6 | `..\lunia_z\Reference\Tools\ComfyUI-0.34.6` | 옆 프로젝트가 2026-09-08에 받아 검증한 설치와 모델(15GB)을 빌려 쓴다. `IG_COMFYUI`로 덮어쓸 수 있다 |
| 콘텐츠 전용 UE 프로젝트 | `%LOCALAPPDATA%\IndieGame\ArtImport` | `Import-BlenderAssets.ps1`이 만들고 관리한다 |

## 명령

```powershell
# 1. 절차 빌더 전부(또는 -Only unit_door, SM_MailboxUnit …)
powershell -File Scripts\Build-BlenderAssets.ps1

# 2. 생성 경로: 서버를 띄우고, 기준 이미지 한 장으로 형상을 뽑고, 다듬는다
powershell -File Scripts\Start-ComfyNative.ps1
python Scripts\generate_3d_comfy.py --image Content\SourceArt\Generated\X\source-front.png --name X --route trellis --resolution 1024
blender -b --factory-startup --python Scripts\blender\refine_generated.py -- --glb Content\SourceArt\Generated\X\trellis1024-s56\raw\pbr_00001_.glb --name SM_X --mesh-class hero --height 172 --yaw 0 --probe   # 축 확인
blender -b --factory-startup --python Scripts\blender\refine_generated.py -- --glb … --name SM_X --mesh-class hero --height 172 --yaw 0 --vertex-ao

# 3. UE 반입(콘텐츠 전용 프로젝트 → Content 복사 → Docs/mesh_bounds.json 갱신)
powershell -File Scripts\Import-BlenderAssets.ps1            # 전부
powershell -File Scripts\Import-BlenderAssets.ps1 -Only SM_X # 골라서

# 4. 게임 코드가 그 메시를 쓰게 고쳤으면 컴파일과 감사
powershell -File Scripts\Build-ArtAssets.ps1 -CodeOnly
python Scripts\audit_world_geometry.py --check
```

## 배치 확인

에디터를 못 여는 동안 「메시가 씬 제자리에, 제 크기와 방향으로 들어갔는지」는
Blender로 본다. 지오메트리 감사가 푼 상자 전부(벽·바닥·상자 소품)와 저작 메시를
UE 좌표 그대로 세워 EEVEE로 찍는 것이라, 감사가 보는 세계와 같은 것을 본다.

```
python Scripts/audit_world_geometry.py --export-layout Saved/scene_layout.json
blender -b --factory-startup --python Scripts/blender/render_scene_layout.py -- Saved/scene_layout.json Saved/scene_preview [view ...]
```

카메라 자리는 `render_scene_layout.py`의 `VIEWS`에 있다(편의점 다섯, 403호 셋,
복도·로비·골목, 그리고 천장을 잘라 낸 평면도 셋). 상자는 재질 이름으로 색을
정하고 저작 메시는 저장된 `.blend`와 구운 D/ORM을 쓴다. 폴백 블록아웃은 빼야
메시가 보이므로, 저작 메시 `if` 뒤의 `else` 블록에는 반드시
`// physics-audit: intentional 저작 메시가 없을 때만 짓는 폴백이다. …` 표식을
호출 위 8줄 안에 둔다. 표식은 같은 평면 감사도 읽는다 — if/else 두 가지를 동시에
세워 놓고 서로 깜빡인다고 잡는 오탐이 그것으로 사라진다.

Blender 원본이 없는 메시(스캔 소품, 예전 지오메트리 스크립트 메시)는 구운
바운드 상자로만 서고, 감독이 스폰하는 소품(쪽지·병)은 CreateBlock이 아니라
안 보인다. 금속은 하늘이 없으면 검게 죽으므로 월드를 밝은 회색으로 둔다.

### raw_uv 메시

`build_asset(..., raw_uv=True)`는 스마트 UV도 굽기도 하지 않고 빌더가 편 UV0을 그대로
내보낸다. 반입 스크립트는 텍스처가 없는 manifest를 보면 MI를 만들지 않고 재질을 씬에
맡긴다. 라벨을 원통으로 감는 슬리브, 씬의 UV 재질을 읽는 뚜껑처럼 UE 재질이 UV0을 직접
읽어야 하는 것에만 쓴다. 상품 앞면 그림은 이 경로가 아니라 image_quad로 붙여 굽는다.

## 좌표와 원점 규약

- 빌더는 **UE 좌표로** 그린다. m 단위, X·Y·Z가 UE의 cm/100이다.
  `build_probe_axes.py`로 실측한 결과 기본 FBX 경로는 Y만 뒤집히므로,
  `build_asset`이 결합 직후 정점 Y를 거울 반전한다. UE가 다시 뒤집어 빌더가
  적은 좌표가 그대로 UE 좌표가 된다. 굽기는 반전 뒤에 하므로 노멀맵도 UE가
  보는 기하 그대로다.
- 원점은 바닥 중심, 앞면은 -Y가 기본이다. 씬 좌표계가 다른 것(냉장고·주방은
  앞면 -X, 천장 등은 윗면 원점, 문짝은 힌지 축)은 manifest의 `origin`·`notes`에
  적고, 씬 코드가 그 관례대로 놓는다.
- 씬은 `CreateBlock(원점, FVector(100, 100, 100), nullptr, 충돌, 메시)`로 놓는다.
  크기 100은 배율 1이고, `nullptr` 재질은 「메시가 들고 온 인스턴스를 써라」다.
  `audit_world_geometry.py`는 `PropMesh(TEXT("SM_X"))` 바인딩과
  `Docs/mesh_bounds.json`으로 실제 상자를 세운다.

## 재질과 텍스처

- 절차 재질(도장 강판·스테인리스·플라스틱·고무·유리·발광·테라조)은
  `ig_blender_lib.py`의 `mat_*`다. 한 에셋의 baked 재질은 전부 한 장의
  `_D`(sRGB) / `_N`(DirectX, G 뒤집음) / `_ORM`(AO·거칠기·금속성) / `_E`로
  굽히고, UE 마스터 `M_IGBakedProp`의 인스턴스 `MI_<Name>`이 슬롯 `Baked`에
  걸린다. 유리는 슬롯 `Glass`로 남겨 기존 `M_Glass`가 걸린다.
- 생성 시트를 붙이려면 `image_quad` + `mat_image_uv`. 별도 UV 층 `ImageUV`로
  읽어서 스마트 UV가 활성 층을 새로 펴도 이미지가 제자리에 들어간다.
- 한글 글자는 `text_mesh`(맑은 고딕)로 얇은 기하를 세운다. 텍스처에 글자를
  굽지 않는다는 규칙은 그대로다 — 글자는 기하다.
- 발광은 8비트 이미지에서 1.0에서 잘리므로 manifest의 `emissive_strength`가
  인스턴스의 `EmissiveStrength`로 간다.

## 충돌·LOD

- 충돌은 빌더가 `UCX_<Name>_NN` 볼록 껍데기로 낸다. 5.8은 FBX를 Interchange로
  넘기고 그 경로는 UCX를 버리므로, 임포트 스크립트가
  `Interchange.FeatureFlags.Import.FBX 0`으로 예전 FBX 임포터를 잡는다.
- LOD는 `mesh_lod_contract.py`와 같은 사슬이다. manifest의 `mesh_class`
  (hero 12000 / prop 3000 / large 9000)가 우선한다.

## 생성 경로의 규칙

- 라이선스: TRELLIS.2(MIT)·DINOv3(Meta 제한 허가)·BiRefNet(MIT)만 쓴다.
  Hunyuan3D는 커뮤니티 라이선스가 한국을 제외하므로 쓰지 않는다. 모델 파일
  해시는 lunia_z의 `SourceArt/Manifests/ComfyNative.20260908.json`에 있다.
- 입력은 우리 기준 시트(`Content/SourceArt/AI/Sheet*.png`)의 한 칸이다. 생성
  기록은 `Content/SourceArt/Generated/<name>/<trial>/generation.json`에 남는다.
- `refine_generated.py`가 하는 일: 회전(`--yaw`, `--rot-x`)으로 정면을 -Y·머리를
  +X에, 한 축을 실제 cm에(`--length/--width/--height`), 배경 잘라내기
  (`--clip-y-*`, `--shift`, `--squash`), 저밀도 데시메이트(예산), 고밀도→저밀도
  굽기. 털처럼 얇은 조각으로 깨진 표면은 `--voxel-remesh 0.006`으로 녹인다.
  위층 사람처럼 정점색 AO를 읽는 재질은 `--vertex-ao`.
- 축이 헷갈리면 `--probe`(주축·양끝 높이)와 `--views`(정면·측면·위 세 장)로
  본 뒤 정한다. 생성물은 입력 그림에 따라 눕거나 대각선으로 나온다.
  기는 자세는 프로브가 찍는 양끝 높이 중 높은 쪽이 머리다. 그 끝을 +X로 두고
  반입 뒤 `Docs/mesh_bounds.json`의 extent에서 X가 긴 축인지 다시 본다 — 한 번
  `--yaw -90`을 잘못 걸어 몸이 복도를 가로질러 놓인 채 들어간 적이 있다.

## 리깅된 인물 — 위층 사람

정적 셸 하나에 정면 스프라이트 카드를 겹치던 위층 사람은 2026-09-11부터
뼈대와 동작이 있는 스켈레탈 메시(`SK_ListenerCrawler`)다. 카드는 정면에서만
사람이었고 옆에서는 판이었으며, 셸은 기는데 미끄러졌다.

```powershell
blender -b --factory-startup --python Scripts\blender\rig_crawler.py -- `
    --glb Content\SourceArt\Generated\ListenerPhotoCrawl\photo-20260914\raw\pbr_00001_.glb `
    --name SK_ListenerCrawler --length 190 --yaw 94 --voxel-remesh 0.004 `
    --smooth-iterations 3 --budget 12000 --texture-size 2048 `
    --landmarks Scripts\blender\listener_photo_landmarks.json --keep-largest
powershell -File Scripts\Import-BlenderAssets.ps1 -Only SK_ListenerCrawler
```

`rig_crawler.py`가 하는 일은 refine_generated.py와 같은 앞부분(회전·치수·
원점) 뒤에 넷이다.

- **복셀 리메시 4 mm.** 9월 14일 새 기준 이미지에서 만든 몸을 한 표면으로
  정리하고 몸에서 떨어진 발 조각을 없앴다. 스무딩 세 번 뒤 12,000삼각형으로
  줄이고 색·노멀·거칠기를 2048px D/N/ORM에 굽는다. 고밀도 원본도 부드러운
  노멀과 비금속 표면으로 바꿔 폴리곤 면이 굽기에 남는 일을 줄였다.
- **검수한 관절 좌표.** 사진에서 만든 앞·옆·뒤·위와 얼굴·주먹 확대 시트를
  Blender 렌더와 대조했다. 양팔과 다리 자세가 비대칭이므로 관절을 통계로만
  추정하지 않고 `listener_photo_landmarks.json`에 지정한다. 좌표는 회전·실제
  치수 적용·Y 미러링이 끝난 저작 공간의 미터 단위다.
- **거리 웨이트.** Blender 자동 웨이트(열 확산)는 리메시 껍질에서 「failed
  to find solution」으로 거의 모든 정점을 비운 채 돌아왔다. 뼈마다 두께
  반지름을 두고 표면에서 뼈 표면까지의 거리로 가우시안(4.5 cm)을 걸어
  최대 넷을 섞는다. 게임과 같은 선형 블렌딩으로 검수한다.
- **접지를 구운 동작 넷.** Crawl(36프레임 루프)은 손목·발목의 지지와 들어
  옮기기를 두 관절 역운동학으로 풀어 키프레임에 기록한다. Listen(90프레임)은
  숨을 쉬면서 손발을 고정한다. Bang(63프레임)은 녹음의 0/0.62/1.24초와
  가장 가까운 0/19/37프레임에 타격한다. Lunge(24프레임)는 몸통과 팔을 함께
  들어 올린다. 이동은 폰이 맡는다. 각 액션의 전 채널을 초기화해 이전 자세가
  섞이지 않게 하고, fake user를 지정해 .blend를 다시 열어도 네 동작이 남는다.

FBX는 뼈대+메시+액션 전부를 테이크로 굽고(`bake_anim_use_all_actions`),
`import_blender_assets.py`가 manifest의 `"skeletal": true`를 보고 예전 FBX
임포터로 스켈레탈 반입한 뒤 테이크를 `A_ListenerCrawler_<Action>`으로
이름 붙인다. 마스터 재질 `M_IGBakedProp`에는 `used_with_skeletal_mesh`가
켜져 있어야 한다 — 없으면 에디터는 경고만 내고 그리지만 패키지는 회색으로
그린다. 반입 스크립트가 켠다.

게임 쪽은 `AIGListenerEntity::BuildSkeletalBody`가 메시와 동작 넷을 싣고,
상태에 따라 재생 배율만 바꾼다(순찰 1.0, 추격 2.8 상한). 두드릴 때 Bang,
대답에 얼면 Listen을 0배속으로 세우고, 추격 중 두 팔 거리 안에 들어오면
Lunge. 스켈레탈 에셋이 없으면 예전 셸+카드로 내려간다.

반입 시 `/Game/Meshes/DA_IGCharacterLODs` 설정으로 LOD 네 단계를 실제
생성한다. 삼각형 목표 비율은 1 / 0.55 / 0.25 / 0.10, 전환 화면 점유율은
0.30 / 0.12 / 0.045다. 네 단계와 감소하는 정점 수를 검사한 뒤에만 저장한다.
이번 반입의 정점 수는 14,400 / 9,377 / 5,242 / 2,930이다. UV 경계 등의 정점
분할 때문에 정점 비율과 삼각형 목표 비율은 같지 않다.

미리보기: `SK_ListenerCrawler_bones.png`(뼈 자리), `_crawl_f01~f28.png`(기는
네 프레임), `_lunge.png`, `_bang.png`. `--out`은 작업 폴더 기준 절대 경로로
변환한다. 과거 Blender가 드라이브 루트에 쓰던 상대 경로 오류를 고쳤다.

사진과 생성 시트의 출처는 [9월 14일 검증 기록](REALISM_REVIEW_2026-09-14.md)에
있다. 목한수의 표면을 같은 방식으로 다시 굽는 명령은 다음과 같다.

```powershell
blender -b --factory-startup --python Scripts\blender\refine_generated.py -- `
    --glb Content\SourceArt\Generated\MokHansoo\trellis1024-s56\raw\pbr_00001_.glb `
    --name SM_MokHansooFigure --height 172 --yaw 0 --organic `
    --voxel-remesh 0.003 --smooth-iterations 2 --budget 12000 --texture-size 2048
pwsh -NoProfile -File Scripts\Import-BlenderAssets.ps1 -Only SM_MokHansooFigure
```

## 지금까지 바꾼 것

| 에셋 | 경로 | 씬 |
|---|---|---|
| SM_UnitDoorLeaf / L / Frame | 절차 | 401·402·403 현관, AIGSwingDoor. 문짝은 판 앞으로 4 mm 안쪽만 나온다 |
| SM_UnitDoorHardware / L | 절차 | 레버·도어락. 문짝과 원점이 같아 같은 자리에 충돌 없이 놓는다. 문짝에서 뗀 이유는 `build_unit_door.py` 머리말 |
| SM_FireExtinguisherBox / SM_FireExtinguisher | 절차 | 복도 남쪽 벽, 밤1 낙하 물리 소품 |
| SM_MailboxUnit | 절차 | 로비 북쪽 벽 |
| SM_CeilingLightRing / Dome | 절차 | 복도 넷·로비 둘. 돔 슬롯은 씬이 M_LightPanel로 덮는다 |
| SM_FridgeBody / Door | 절차 | AIGFridge |
| SM_KitchenBaseRun 등 주방 7종 | 절차 | BuildApartment |
| SM_ApartmentWindow / SM_VenetianBlind | 절차 | 403호 북쪽 창. 유리는 씬 발광판, 블라인드 원점은 헤드레일 윗면 |
| SM_VideoIntercom / SM_WallSwitch / SM_ShoeCabinet | 절차 | 403호 현관 벽(Y -215). 인터폰 앞면은 생성 패널 그림 |
| SM_StoreCoolerBank / Door, SM_StoreGondola, SM_StoreCounter, SM_CardTerminal, SM_HotSnackWarmer, SM_ChestFreezer, SM_OpenShowcase | 절차 | BuildStore. 선반 윗면 높이가 씬 상품 배치와 같다. 열린 칸 문짝은 힌지 원점에 yaw 120. 단말기·온장고는 상판 위(Z 99) 별도 메시라 계산대 바운드가 상판에서 끝난다 |
| SM_VillaWindow | 절차 | 골목 빌라 파사드 창 열다섯 자리. 유리 판은 씬 상자 |
| SM_UtilityPole | 절차 | 골목 전주 둘. 분전함은 스캔 소품 |
| SM_GasMeterBox / SM_AcOutdoorUnit / SM_ConvexMirror | 절차 | 골목 샛길 둘의 벽 소품. 거울면은 금속이라 루멘이 비춘다 |
| SM_TriangleKimbapA~D | 절차 | 편의점 삼각김밥. 김 필름은 `AI/KimbapFilmAlbedo_20260916.png`, 앞뒤 인쇄와 개봉 번호는 `UtilityPrints/Kimbap*.png`를 붙여 굽는다(`build_detail_props.py`) |
| SM_TobaccoCabinet, SM_WindowBar, SM_HotWaterDispenser, SM_TrashBin | 절차 | 계산대 뒤 담배 진열장(담뱃갑 192), 창가 취식대, 온수기, 2구 쓰레기통 |
| SM_DeliveryScooter / SM_DeliveryScooterWheel / SM_DeliveryRider | 절차 | 골목 배달 스쿠터와 기사(`build_delivery_scooter.py`). 바퀴는 따로 돌리려고 뗐다. 충돌은 없고 막힘 상자를 `AIGNeighborhoodLifeDirector`가 둔다. 기사는 1초 남짓 스쳐 가는 대역이라 생성 인물로 바꿀 자리다 |
| SK_ListenerCrawler | 생성+리깅 | 위층 사람의 기는 몸. 위 「리깅된 인물」. Crawl·Listen·Bang·Lunge |
| SM_ListenerEntityCrawl | 생성 | 위층 사람(정적 폴백). 해부 시트의 옆모습 칸에서 뽑았다(앞모습 3/4 칸은 네 발 짐승처럼 읽혔다). 폰의 앞이 +X라 머리가 +X에 와야 한다. 프로브에서 높은 끝이 이미 +X면 `--yaw 0`, 길이는 `--length 190`. 정점 AO, 석고 재질은 그대로 |
| SM_AlleyCatRun | 생성 | 골목 고양이. 구운 털 색을 MI로 쓴다 |
| SM_MokHansooFigure | 생성 | 밤4 목한수 통짜. 조각 셋과 카드를 대체 |
| SM_FinalCavityRemains | 생성 | 밤4 공동 유해 통짜. 조각 넷과 카드를 대체 |

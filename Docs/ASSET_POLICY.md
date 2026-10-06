# Asset and License Policy

외부 에셋은 다운로드 시점에 상업적 게임 배포, 수정, 플랫폼 패키징 권한을 확인한 뒤 저장소에 추가합니다. 출처를 기억에 의존하지 않습니다.

## 반입 규칙

- 라이선스 원문 또는 영수증을 프로젝트 외부의 안전한 보관소에도 보존합니다.
- 재배포 금지 에셋은 원본 파일을 공개 저장소에 올리지 않습니다.
- 생성형 AI 결과물은 사용한 서비스, 생성 날짜와 당시 약관을 기록합니다.
- 사람의 얼굴·목소리·상표·실제 주소가 포함된 자료는 별도 초상권/상표권/개인정보 검토를 거칩니다.
- 코드 라이선스와 콘텐츠 라이선스를 구분합니다.

## 에셋 대장

| 프로젝트 경로 | 제작자/출처 | 라이선스 | 취득일 | 수정 여부 | 증빙 위치 | 비고 |
|---|---|---|---|---|---|---|
| `/Game/Prototype/Textures/T_Photo_Asphalt_*` | ambientCG.com — Asphalt012 | CC0 1.0 | 2026-07-19 | 원본(2K JPG) | `Content/SourceArt/Photo/Asphalt/` | 골목 노면 |
| `/Game/Prototype/Textures/T_Photo_Concrete_*` | ambientCG.com — Concrete034 | CC0 1.0 | 2026-07-19 | 원본(2K JPG) | `Content/SourceArt/Photo/Concrete/` | 외벽/매장 벽 |
| `/Game/Prototype/Textures/T_Photo_Jangpan_*` | ambientCG.com — WoodFloor051 | CC0 1.0 | 2026-07-19 | 원본(2K JPG) | `Content/SourceArt/Photo/Jangpan/` | 원룸 장판 |
| `/Game/Prototype/Textures/T_Photo_MetalBrushed_*` | ambientCG.com — Metal032 | CC0 1.0 | 2026-07-19 | 원본(2K JPG) | `Content/SourceArt/Photo/MetalBrushed/` | 금속 표면 |
| `/Game/Prototype/Textures/T_Photo_Blanket_*` | ambientCG.com — Fabric022 | CC0 1.0 | 2026-07-19 | 원본(2K JPG) | `Content/SourceArt/Photo/Blanket/` | 침구 원단 |
| `/Game/Prototype/Textures/T_Photo_WoodDark_*` | ambientCG.com — Wood067 | CC0 1.0 | 2026-07-19 | 원본(2K JPG) | `Content/SourceArt/Photo/WoodDark/` | 가구 목재 |
| `/Game/Prototype/Textures/T_Photo_MarbleFloor_*` | ambientCG.com — Marble016 | CC0 1.0 | 2026-07-26 | 원본(2K JPG) | `Content/SourceArt/Photo/MarbleFloor/` | 승강기 바닥 대리석. `M_MarbleFloor_XY` |
| `/Game/Audio/S_*` (67종: 발소리 6면·문·노크·위층 사람·놀람·베드) | OpenGameArt rubberduck 「100 CC0 SFX」 1·2·wood-metal, Kenney 「Impact Sounds」, Owlish Media 「Sound Effects Pack」 | CC0 1.0 | 2026-09-11 | 가공(피치·저역·겹침·되울림·루프 이음) | `Content/SourceArt/Audio/manifest.json`, `Scripts/curate_cc0_audio.py` | 원본 팩은 `Saved/AudioCC0/`에 두고 저장소에는 가공본만 둔다. 게임은 `IGAudio::Sample`로 찾고 없으면 합성기 |
| `/Game/Meshes/SK_ListenerCrawler`, `A_ListenerCrawler_{Crawl,Listen,Bang,Lunge}`, `DA_IGCharacterLODs` | 직접 제작 (사진 참고 → gpt-image 기준 이미지 → TRELLIS.2 → Blender 표면 정리·접지 리깅) | 프로젝트 소유 | 2026-09-14 | 새 원본으로 교체 | `Content/SourceArt/Blender/SK_ListenerCrawler/`, `Scripts/blender/rig_crawler.py` | 12,000삼각형, 21개 뼈, 네 동작, 스켈레탈 LOD 네 단계. `Docs/BLENDER_PIPELINE.md` 「리깅된 인물」 |
| `Content/SourceArt/Reference/Listener/*` | Wikimedia Commons — 미 육군·해병대 낮은 포복 사진, 살아 있는 조각상 사진 | 파일별 공개 저작물·CC 라이선스, 참고 전용 | 2026-09-11 | 원본 | 파일명이 Commons 파일명 | 이번에 채택한 두 사진의 정확한 출처와 공개 조건은 `Docs/REALISM_REVIEW_2026-09-14.md`. 다른 사진의 라이선스까지 같은 것으로 간주하지 않는다 |
| `Content/SourceArt/AI/ListenerPhotoAnchor_20260914.png`, `ListenerTurnaround_20260914.png` | OpenAI 내장 ImageGen, gpt-image 스킬 흐름 | 생성 원본, 프로젝트 제작 자료 | 2026-09-14 | 원본 | `Docs/REALISM_REVIEW_2026-09-14.md` | 가상 인물의 기준 이미지와 다각도·확대 시트. 게임 패키지에 직접 포함하지 않는다 |
| `/Game/Photo/Props/*` (9종: old_bed_frame, side_table_01, metal_office_desk, painted_wooden_chair_01, desk_lamp_arm_01, trashbag, cardboard_box_01, plastic_crate_01, utility_box_01) | polyhaven.com (포토그래메트리 스캔) | CC0 1.0 | 2026-07-19 | 원본(glTF, 1K 텍스처) | `Content/SourceArt/PhotoProps/` | 실물 스캔 소품 |
| `/Game/Meshes/SM_*` (30종: 생활 소품·밸브 손잡이·고양이·위층 사람·M5 공동 잔존물·목한수 근접 대치) | 직접 제작 (UE5 Geometry Script 절차 모델링) | 프로젝트 소유 | 2026-08-05 | 원본 | `Scripts/generate_meshes.py` | 회전체·베벨·불리언·스윕. ImageGen 비율 기준과 실제 치수 계약에 맞춰 절차 메시로 재구성 |
| `/Game/Meshes/SM_UnitDoorLeaf·WideL, SM_UnitDoorHardware·WideL, SM_UnitDoorFrame·Wide, SM_FireExtinguisherBox, SM_FireExtinguisher, SM_MailboxUnit, SM_CeilingLightRing·Dome, SM_FridgeBody·Door, SM_KitchenBaseRun, SM_DrumWasher, SM_KitchenWallUnits, SM_RangeHood, SM_Microwave, SM_KitchenSink, SM_InductionHob, SM_Wardrobe, SM_WallAirConditioner, SM_TrafficCone, SM_StoreCoolerBank·Door, SM_StoreGondola, SM_StoreCounter, SM_CardTerminal, SM_HotSnackWarmer, SM_ChestFreezer, SM_OpenShowcase, SM_ApartmentWindow, SM_VenetianBlind, SM_VideoIntercom, SM_WallSwitch, SM_ShoeCabinet, SM_VillaWindow, SM_UtilityPole, SM_GasMeterBox, SM_AcOutdoorUnit, SM_ConvexMirror, SM_TriangleKimbapA~D, SM_TobaccoCabinet, SM_WindowBar, SM_HotWaterDispenser, SM_TrashBin` (49종) | 직접 제작 (Blender 5.2 헤드리스 절차 모델링, Cycles 베이크) | 프로젝트 소유 | 2026-09-04 | 원본 | `Content/SourceArt/Blender/<이름>/`, `Scripts/blender/build_*.py` | 실제 치수·베벨·UCX 충돌·구운 D/N/ORM. `Docs/BLENDER_PIPELINE.md` |
| `/Game/Meshes/SM_DeliveryScooter, SM_DeliveryScooterWheel, SM_DeliveryRider` | 직접 제작 (Blender 헤드리스 절차 모델링, Cycles 베이크) | 프로젝트 소유 | 2026-10-06 | 원본 | `Content/SourceArt/Blender/<이름>/`, `Scripts/blender/build_delivery_scooter.py` | 골목 배달 스쿠터. 차체·바퀴·기사를 나눠 바퀴만 돌린다. 충돌 없음 |
| `/Game/Meshes/SM_ListenerEntityCrawl, SM_AlleyCatRun, SM_MokHansooFigure, SM_FinalCavityRemains` | 직접 제작 (기준 시트 한 칸 → ComfyUI 네이티브 TRELLIS.2 형상 생성 → Blender 다듬기) | 프로젝트 소유. TRELLIS.2 가중치 MIT(Microsoft, Comfy-Org 재포장), DINOv3 Meta 제한 허가, BiRefNet MIT | 2026-09-08 | 원본 | `Content/SourceArt/Generated/<이름>/<시도>/generation.json`, `Scripts/generate_3d_comfy.py`, `Scripts/blender/refine_generated.py` | 입력은 우리 기준 시트뿐. Hunyuan3D는 한국 제외 라이선스라 쓰지 않는다 |
| `/Game/Prototype/Textures/T_<Blender 에셋>_{D,N,ORM,E}` · `/Game/Prototype/Materials/M_IGBakedProp, MI_*` | 직접 제작 (Cycles 베이크, UE 마스터 재질 인스턴스) | 프로젝트 소유 | 2026-09-04 | 원본 | `Scripts/import_blender_assets.py` | 에셋마다 한 세트. ORM은 AO·거칠기·금속성 채널 |
| `/Game/Prototype/Textures/T_Label* · T_Snack*` | 직접 제작 (System.Drawing) — 가상 브랜드, 실제 상표 미사용 | 프로젝트 소유 | 2026-07-25 | 원본 | `Scripts/Create-RetailGraphics.ps1`, `Scripts/Prepare-AIArt.ps1` | 제품 라벨·봉지 아트. 소주 라벨은 7월 스크립트로 그린 PNG만 인쇄 아틀라스 원본으로 남아 있다 |
| `Content/SourceArt/Labels/Store/CigarettePacks.png` (담뱃갑 앞면 8) | 직접 제작 (PIL) — 가상 브랜드, 실제 상표·전화번호 미사용, 담뱃갑 경고면은 글자만 | 프로젝트 소유 | 2026-09-08 | 원본 | `Scripts/create_store_product_art.py` | 담배 진열장 빌더가 담뱃갑 앞면에 붙여 굽는 입력. 게임 텍스처로 직접 쓰지 않는다 |
| `/Game/Prototype/Textures/T_Sign* · T_Poster* · T_Note*` | 직접 제작 (System.Drawing + 시스템 폰트) | 프로젝트 소유 | 2026-07-19 | 원본 | `Scripts/Create-SignTextures.ps1` | 한글 간판·포스터 |
| `/Game/Prototype/Textures/T_(Jangpan·Wallpaper·…)_{D,N,R}` | 직접 제작 (절차 생성) | 프로젝트 소유 | 2026-07-19 | 원본 | `Scripts/generate_surface_textures.py` | 사진 텍스처 폴백 |
| `Source/IndieGame/UI/Fonts/Pretendard-{Regular,SemiBold}.otf` | orioncactus / Pretendard 1.3.9 | SIL Open Font License 1.1 | 2026-08-12 | 원본 | `Source/IndieGame/UI/Fonts/OFL-Pretendard.txt` | 설정·대화·본문·보조문구. 패키지 실행 파일 옆 `UI/Fonts`에 NonUFS 스테이징 |
| `Source/IndieGame/UI/Fonts/GowunBatang-Bold.ttf` | Yanghee Ryu / Google Fonts | SIL Open Font License 1.1 | 2026-08-12 | 원본 | `Source/IndieGame/UI/Fonts/OFL-GowunBatang.txt` | 타이틀·장면 제목 전용. 패키지 실행 파일 옆 `UI/Fonts`에 NonUFS 스테이징 |

ambientCG 자료는 CC0 1.0(상업적 사용·수정·재배포 허용, 출처 표기 불요)입니다. 내려받은 zip은 `Scripts/Get-PhotoTextures.ps1`이 풀 때만 쓰고 저장소에는 두지 않으며, 풀어 둔 JPG가 `Content/SourceArt/Photo/`에 있습니다.

두 한글 서체는 OFL 1.1에 따라 상업적 사용·수정·재배포가 가능하며 폰트 파일과
라이선스 원문을 함께 배포한다. Pretendard는 내비게이션과 긴 문장의 중립적
가독성을, 고운바탕 Bold는 「없는 층」 제목의 문학적 긴장만 담당한다. 의미 있는
본문을 생성 이미지에 굽지 않는 기존 원칙은 그대로 유지한다.

## 생성형 이미지(ImageGen) 아트워크

OpenAI ImageGen으로 생성하고 각 항목의 생성 방식과 날짜를 아래 기록에
남겼습니다. `Scripts/Prepare-AIArt.ps1`로 크롭·리샘플·미세문구 재작성을
진행하며, 원본은 `Content/SourceArt/AI/`에 그대로 보관합니다.

| 원본 | 산출 텍스처 | 내용 |
|---|---|---|
| `LabelWater_raw.png` | `T_LabelWater_D` | 새벽샘물 생수 라벨 (미세문구 재작성) |
| `LabelRamyeon_raw.png` | `T_LabelRamyeon_D` | 왕라면 컵라면 라벨 |
| `SignMain_raw.png` | `T_SignMain_D` | 새벽24 무영로점 파사드 간판 |
| `SheetSnacks.png` | `T_SnackShrimp_D` · `T_SnackPotato_D` · `T_SnackSquid_D` · `T_SnackCorn_D` | 과자 봉지 4종 (새우빵·감자스낵·오징어칩·콘스낵) |
| `SheetBottles.png` | `T_LabelGreenTea_D` · `T_LabelBarley_D` · `T_LabelSoda_D` · `T_LabelSoju_D` | 음료 라벨 4종 (산들녹차·구수한보리·톡소다·새벽이슬) |
| `SheetSigns.png` | `T_SignLaundry_D` · `T_SignHair_D` · `T_SignHof_D` · `T_SignSuper_D` · `T_SignPC_D` · `T_SignKaraoke_D` | 골목 상가 간판 6종 |
| `SheetPosters.png` | `T_PosterSale_D` · `T_PosterRamyeon_D` · `T_PosterFlyer_D` · `T_NoticeRent_D` | 편의점·골목 인쇄물 4종 |
| `SheetPaperNotes_v2.png` | `T_PaperClean_V2_D` · `T_PaperWet_V2_D` · `T_PaperFolded_V2_D` · `T_PaperOld_V2_D` | CH02 문서용 빈 종이 4종(깨끗함·젖음·접힘·낡음). 한국어와 영수증 정보는 런타임 텍스트로 표시 |
| `SheetHorrorSurfaceBlends.png` | `T_DecalDampWallpaper_D` | 네 칸 가운데 젖은 벽지 얼룩 한 칸만 쓴다(`M_DecalDampWallpaper`). 마젠타 키 제거 후 RGBA 마스크드 오버레이로 사용 |
| `SheetEvidenceProps.png` | 직접 텍스처로 사용하지 않음 | 뿔테 안경·점검봉·금 간 휴대폰·편의점 봉지를 한 장에 그린 형상·재질 기준. 지금은 금 간 휴대폰(`SM_CrackedPhone`)만 이 시트로 만든다 |
| `SheetAlleyCatPoseReference.png` | 직접 텍스처로 사용하지 않음 | 동일한 고등어태비의 좌측 달리기·정면 3/4·정지·후면 3/4 비례 기준. `SM_AlleyCatRun` 정적 메시로 재구성 |
| `SheetFirstPersonKnockPhases_v1.png` · `v1_RGBA.png` · `SheetFirstPersonKnockPhases_v2.png` · `v2_RGBA.png` | `T_FPHandKnock0_D` · `T_FPHandKnock1_D` · `T_FPHandKnock2_D` · `T_FPHandKnock3_D` | M0 Q/B 두드리기의 같은 오른손·후드 소매 준비/예비/접촉/반동 4단계. v1은 화면 안 소매 절단면 때문에 증빙 전용, v2가 런타임 원본. UI-space 전용 RGBA이며 문·벽·인물·동물·배경을 평면으로 대체하지 않음 |
| `SheetRooftopUnlockedPadlockKeysReference.png` | 직접 텍스처로 사용하지 않음 | 금속 열쇠와 분리링의 형상 기준. 관리실 열쇠 꾸러미 `SM_BoothKeyring`(`Scripts/blender/build_utility_fixtures.py`)을 만들 때 대조했다 |
| `TextureP3CabinetPaintedSteel.png` | `T_P3CabinetPaintedSteel_D` | 회녹색 도장 아연강판과 억제된 습기·잔흠집 알베도. `M_P3CabinetMetalUV`로 넷째 밤의 밸브 손잡이·캐스터·조율 망치에 적용 |
| `TextureAlleyCatTabby.png` | `T_AlleyCatTabby_D` | 고등어태비 단모와 좁은 줄무늬의 저채도 알베도. `M_AlleyCatTabbyUV`로 단일 풀 고양이에 적용 |
| `TextureCarrierBagFilm.png` | `T_CarrierBagFilm_D` | 가상 옅은 청색 무늬가 있는 편의점 LDPE 박막. 구매 봉지(`M_CarrierBagFilm`)와 공사 비닐(`M_ConstructionFilm`)이 같이 쓴다 |
| 위 재질 스캔 3종(고양이·봉지·도장 강판)의 `T_*_D` | `T_*_{N,R,A}` 9종 · `T_P3CabinetPaintedSteel_W` | `generate_ai_pbr_maps.py`로 만드는 PBR 동반 채널 10종. 젖음 맵은 알베도·거칠기·노멀 블렌드에 함께 쓴다 |
| `ApplicationIcon_20260930.png` | `Build/Windows/ApplicationIcon.png` · `Application.ico` | 새벽 빌라 옥상의 무단 증축 옥탑방에 불이 하나 켜진 Windows 배포 아이콘. `prepare_application_icon.py`가 옥탑방 쪽을 잘라 1024px 등급 PNG와 16~256px 7단계 ICO를 만든다 |
| `DialogueHUDConcept_v1.png` | UI 아트 디렉션 기준 이미지 | 실제 Shipping 캡처의 디버그형 대화창을 낮은 하단 점유율, 분리된 환경음 캡슐, 작은 화자 태그와 습기 낀 smoked-glass 재질로 재설계한 시안. 런타임 텍스트를 굽지 않고 색·여백·질감 기준만 사용 |
| `TextureHudDialogueFilm.png` | `T_HudDialogueFilm_D` | 대화창 표면의 저대비 charcoal/oxidized-green 미세 필름 스캔. UI 그룹·NoMipmaps·비스트리밍으로 임포트하고 런타임 둥근 마스크 안에서 낮은 알파로만 사용 |
| `TextureMissingFloorJournalPaper_v1.png` | `T_MissingFloorJournalPaper_D` | 「듣는 것들」 전체 화면의 무문자 장부 종이 표면. 16:9 정면 스캔 질감만 쓰고 모든 한글·출처 카드·썸네일·교차선은 런타임이 그린다. UI 그룹·NoMipmaps·Clamp·비스트리밍으로 임포트 |
| `TextureAudioCalibrationWall_v1.png` | `T_AudioCalibrationWall_D` | 첫 실행 소리·밝기 보정판의 무문자 청흑색 빌라 벽면. 저대비 광물 결만 낮은 알파로 사용하고 한글·눈금·암부 계조는 전부 런타임 HUD가 그린다. UI 그룹·NoMipmaps·Clamp·비스트리밍으로 임포트 |
| `ApartmentVisualTarget_v1.png` | 원룸 비주얼 아트 디렉션 기준 이미지 | 실제 Shipping 원룸 캡처의 카메라·동선·가구·HUD는 유지하고, 주황 스탠드와 청록 새벽광, 낡은 벽지·장판의 물성, 국부 습기 흔적만 보강한 목표 시안. 런타임 텍스처로 직접 사용하지 않음 |
| `TextureApartmentWallpaperVintage.png` | `T_ApartmentWallpaperV2_D` | 2000년대 초 한국 빌라의 저가 아이보리 엠보싱 벽지 알베도. 전용 N/R/A 채널과 `M_Wallpaper_X/Y/Ceil`에 연결 |
| `MaskApartmentWallPatina.png` | `T_ApartmentWallPatina_M` | 원룸 하부 모서리에 제한한 습기·들뜸 마스크. `M_ApartmentWallPatina`의 불투명도·색·거칠기 변화에 사용하고 충돌 없는 근거리 평면으로 배치 |
| `SheetMissingFloorEnvironmentReference.png` | 직접 텍스처로 사용하지 않음 | 「없는 층」의 같은 빌라 외관·4F 복도·불법 5층·옥상 통로의 카메라·재료·노출 기준. 실제 충돌 지오메트리는 코드가 소유 |
| `SheetListenerEntityAnatomyReference.png` | `SM_ListenerEntityCrawl` | 위층 사람의 정·측·상·3/4 인체 연결 기준. 얼굴·피부·고어 없이 연속 3D 메시와 건식 석고 PBR로 재구성 |
| `ListenerEntityFrontCutout.png` | `T_SpriteListenerFront_{D,N,R,A}` · `M_SpriteListenerFront` | 위층 사람의 정면 복도 판독용 PBR 레이어. 1.6m에서 켜고 활성화 뒤 1.25m까지 유지하며, 3D 셸·그림자는 계속 보존하고 측면에서는 숨김 |
| `SheetListenerEntityCrawlPhases.png` | `T_SpriteListenerCrawl0..3_{D,N,R,A}` · `M_SpriteListenerCrawl0..3` | 같은 인물의 좌우 팔꿈치 지지·중앙 지지·회복 네 자세. 속도 연동 1.6~6fps, 정지 프레임 유지, 응답 노크 대기 시 중앙 자세 고정 |
| `TextureMissingFloorDryPlaster.png` | `T_MissingFloorDryPlaster_{D,N,R,A}` | 불법 5층과 존재의 건식 석고 표면. 조명 없는 타일 알베도에서 PBR 동반 채널 생성 |
| `SheetMissingFloorResidueMasks.png` | `T_MissingFloorHandprints_M` · `T_MissingFloorDragTrails_M` · `T_MissingFloorDustJoint_M` · `T_MissingFloorCavityScratches_M` | 손자국·끌림·분진 이음·공동 긁힘 값 마스크. 실표면에 masked 블렌드 |
| `SheetMissingFloorDistantCharacters.png` | `T_SpriteSeo_D` · `T_SpriteMok_D` | 접근 불가 12m 이상 고정 컷용 RGBA 인물. 네 칸 가운데 서일영과 목한수만 자른다 |
| `SheetMissingFloorHeroPropsReference.png` | `SM_TuningHammer` · `SM_TunerToolCart` · `SM_ComplaintLedger` · `SM_CalendarJournal` | 조율 렌치·공구 카트·민원 원장·달력 일지의 실제 두께·접지·시차를 가진 3D 프롭 기준 |
| `TextureVillaStairCheckerPlatePaintedSteel.png` | 미사용 (증빙 보존) | v1. 계약은 전부 통과했으나 손전등 프레임에서 디딤판이 매끈한 판으로 보여 폐기. 계조 범위가 11%뿐이라 게인으로도 살아나지 않았다 |
| `TextureVillaStairCheckerPlatePaintedSteel_v2.png` | `T_MissingFloorSteelStair_{D,N,R,A}` | 도장 체커플레이트 철제 계단 디딤판. `Footstep.MetalStair` 표면이 복도 콘크리트로 그려지던 것을 교체한다. 55cm 타일에 다이아몬드 16개(피치 34mm), 거칠기 0.68로 콘크리트(0.9+)와 손전등 반사가 갈린다 |
| `TextureRooftopUrethaneWaterproofing.png` | `T_RooftopWaterproofing_{D,N,R,A}` | 옥상 녹색 우레탄 방수 도막. `Footstep.Rooftop` 전용이며 1.5m 타일. 물 고임 자국은 이미지에 굽지 않고 균일 분포로만 둔다 |
| `TextureApartmentEntranceDoorCharcoalSteel.png` | `T_UnitDoorPaintedSteel_{D,N,R,A}` | 세대 현관문 문짝의 무광 도장 강판. 브러시드 스테인리스를 대체하며 밴드·인레이·레버·도어록·도어스코프는 기존 3D 기하를 유지한다. 발치 마모는 타일이 아니라 별도 masked 평면 |
| `SheetVillaCorridorFixturesReference.png` | 직접 텍스처로 사용하지 않음 | 세대 현관문·우편함 3x3·소화전함·천장 LED 등의 비례·재질 기준. Blender 절차 메시로 재구성 (2026-09-04) |
| `SheetOneroomKitchenAppliancesReference.png` | 직접 텍스처로 사용하지 않음 | 소형 냉장고·빌트인 주방·전자레인지·인터폰 기준. `SM_FridgeBody·Door`, 주방 7종으로 재구성 |
| `SheetStoreFixturesReference.png` | 직접 텍스처로 사용하지 않음 | 편의점 음료 냉장고·곤돌라·평대 냉동고 기준. `build_store_fixtures.py` 7종으로 재구성 (2026-09-08) |
| `SheetApplianceControlPanels.png` | `panels/{DoorLockKeypad,MicrowavePanel,WasherPanel,IntercomFace}.png` | 도어락 키패드·전자레인지·세탁기·인터폰 앞면 정면 텍스처. 세탁기·전자레인지 메시에 `image_quad`로 붙여 굽는다. 한글은 헤드라인급이라 검수만 했다 |

## 아틀라스·LOD·물리 배치 계약

인쇄 아트 51장은 `Content/SourceArt/Atlas/`의 공유 페이지로 묶고, 절차
메시와 스캔 프롭은 등급별 삼각형 예산과 저작 LOD 체인을 받는다. 코드로
배치한 월드 지오메트리는 물리적으로 불가능한 배치가 없는지 검사를 통과해야
한다. 세 계약의 수치·검사·실행 순서는 `ASSET_OPTIMISATION.md`에 있다.

한 번의 생성에 5분이 걸리므로 낱장 대신 **격자 시트**로 묶어 뽑고 슬라이스합니다.
현재 134장의 파생 텍스처(기존 85장 + 없는 층 PBR·마스크·디테일 48장 + 최초 실행 보정 배경 1장)와
증거·생물·설비·인체·사고 프롭 기준 시트를 관리합니다. 2026-08-06
추가분은 생성 실패를 그대로 채택하지 않고 슬리퍼 밑창과 빗물 때를 각각
한 차례 수정 생성했습니다.

운용 규칙:

1. **가상 브랜드만.** 프롬프트에 실존 상표·로고·브랜드 색 조합을 요구하지 않으며,
   생성물에 실존으로 보이는 이름이 섞여 들어오면 `Prepare-AIArt.ps1`의 패치
   단계에서 덮어쓴다. 새벽샘물 라벨의 제조원/판매원·본문 카피가 그 사례다.
2. **헤드라인 한글은 검수, 본문 한글은 재작성.** ImageGen은 짧은 한글 제목은
   정확히 렌더하지만 본문은 지어낸다. 제목은 눈으로 확인하고, 의미가 있는
   본문은 Malgun Gothic으로 직접 다시 그린다.
3. **원본을 지우지 않는다.** `Content/SourceArt/AI/`의 `*_raw.png`는 어떤 픽셀이
   생성물이고 어떤 픽셀이 우리 것인지 추적하기 위한 증빙이다.
4. 생성형 이미지의 저작권 지위는 관할에 따라 다르므로, 배포 전 이 표의 항목을
   다시 검토한다.
5. **정사 물증을 그림으로만 고정하지 않는다.** 흔적은 마스크드 평면과
   거칠기 변화로, 안경·점검봉·휴대폰은 실제 정적 메시와 런타임 상태로
   구현한다. 기준 시트는 형상·마모 참고이며 카메라에 직접 노출하지 않는다.

### 원룸 비주얼 패스 생성 기록

- 서비스: OpenAI ImageGen 내장 도구
- 생성일: 2026-08-06
- 보존 원본: `Content/SourceArt/AI/ApartmentVisualTarget_v1.png`,
  `TextureApartmentWallpaperVintage.png`, `MaskApartmentWallPatina.png`
- SHA-256: 목표 시안
  `01562DEE4BCD53C9A44663AEADB81BAB5B582FABD26D82071BBE91FDFB55CBA5`,
  벽지 `464803BCFE96602303AF292E03FC5308C46C972EC9249B6C78CFAB5CFA91BB03`,
  파티나 `1C8D8B6E1F9849DE838A67BC3D712E71B6ECA5F39D2412A815377BDC04773D17`
- 목표 시안 프롬프트: 실제 Shipping 원룸의 카메라·기하·동선·소품과 HUD를
  보존하고, 따뜻한 텅스텐 조명과 차가운 새벽광, 오래된 벽지·장판의 미세
  물성만 보강한다. 괴물·고어·새 가구·새 출입구는 추가하지 않는다.
- 재질 프롬프트: 벽지는 조명과 그림자를 굽지 않은 이음매 없는 아이보리
  엠보싱 알베도, 파티나는 검정 무손상·흰색 손상의 그레이스케일 마스크로
  생성한다. 의미 있는 문자·상표·인물은 포함하지 않는다.
- 적용: `Build-ArtAssets.ps1 -ApartmentVisualOnly`가 5개 텍스처와 4개
  머티리얼을 빌드·감사한다. 파티나는 두 곳에만 국부 배치하고 9.5m에서
  컬링하며, 조명·상호작용·충돌·서사 상태에는 영향을 주지 않는다.

### Windows 배포 아이콘 생성 기록

- 도구: gpt-image 스킬(Codex 내장 image_gen, ChatGPT 구독 인증)
- 생성일: 2026-09-30. 옛 이야기의 물탱크 점검구 아이콘(2026-08-05)을 바꿨다
- 보존 원본: `Content/SourceArt/AI/ApplicationIcon_20260930.png`와 같은 이름의 JSON
- 파생: `Build/Windows/ApplicationIcon.png`, `Build/Windows/Application.ico`
- 처리: `Scripts/prepare_application_icon.py --crop 0.20 0.12 0.80 0.72`가 옥탑방과
  꼭대기층을 잘라 정사각형으로 맞추고 중간톤 감마, 대비·채도·샤프닝을 고정값으로
  건 뒤 16/24/32/48/64/128/256px ICO 디렉터리를 만든다.
- 검수: 밝은·어두운 작업 표시줄에서 16~48px을 보고 옥탑방 벽과 창 불빛이 남는지
  확인했다.
- 전체 프롬프트: `Docs/IMAGEGEN_PROMPTS_2026-09-30.md`

### 벽지 얼룩·증거 소품 생성 기록

- 서비스: OpenAI ImageGen
- 생성일: 2026-08-03
- 보존 원본: `Content/SourceArt/AI/SheetHorrorSurfaceBlends.png`,
  `SheetEvidenceProps.png`
- 파생: 젖은 벽지 오버레이 1장, 금 간 휴대폰 절차 메시 1종
- 적용: `Scripts/Build-ArtAssets.ps1`이 소스 분리, PBR 파생, 텍스처 임포트,
  마스크드 머티리얼 생성과 Geometry Script 메시 베이크를 순서대로 수행한다.
  마지막 UAsset 감사에서 LOD·해상도·압축·재질 입력과 텍스처 연결을 검사한다.
- 전체 생성·수정 프롬프트: `Docs/IMAGEGEN_PROMPTS_2026-08-03.md`

### 골목 고양이·봉지 재질 생성 기록

- 서비스: OpenAI ImageGen
- 생성일: 2026-08-03
- 보존 원본: `SheetAlleyCatPoseReference.png`, `TextureAlleyCatTabby.png`,
  `TextureCarrierBagFilm.png`
- 파생: 1024 알베도 2장, Geometry Script 정적 고양이 1종
- 적용: 고양이는 0.9~1.35초 풀 이벤트용 단일 정적 메시로 사용하고,
  애니메이션 파이프라인을 추가하지 않는다. 봉지 재질은 구매 봉지와
  공사 비닐이 같이 쓴다.
- 폴백: 메시·머티리얼 미베이크 환경에서는 기존 여섯 도형 고양이와
  유리 봉지 프록시를 유지해 진행을 차단하지 않는다.

### 밸브 손잡이·도장 강판 생성 기록

- 서비스: OpenAI ImageGen
- 생성일: 2026-08-03
- 보존 원본: `Content/SourceArt/AI/TextureP3CabinetPaintedSteel.png`
- 파생: `T_P3CabinetPaintedSteel_D`, `M_P3CabinetMetalUV`,
  `SM_P3ValveWheelLarge`, `SM_P3ValveWheelSmall`
- 적용: 이름은 옛 급수 서비스함(P3)에서 왔다. 서비스함은 옛 이야기와 함께
  빠졌고, 18/12cm 오륜 밸브 손잡이 둘만 넷째 밤 설비와 관리실 수직관에 남았다.

### 관리 열쇠 기준 이미지 생성 기록

- 서비스: OpenAI ImageGen 내장 도구
- 생성일: 2026-08-03
- 보존 원본:
  `Content/SourceArt/AI/SheetRooftopUnlockedPadlockKeysReference.png`
- 적용: 옛 이야기의 옥상 자물쇠 메시는 지웠다. 이미지는 관리실 열쇠 꾸러미
  `SM_BoothKeyring`의 금속 열쇠와 분리링을 대조하는 데만 쓴다.

### CH02 종이 시트 생성 기록

- 서비스: OpenAI ImageGen
- 생성일: 2026-07-26
- 보존 원본: `Content/SourceArt/AI/SheetPaperNotes_v2.png`
- 원본 크기: 1254×1254
- 파생 파일:
  - `Content/Prototype/Textures/T_PaperClean_V2_D.uasset`
  - `Content/Prototype/Textures/T_PaperWet_V2_D.uasset`
  - `Content/Prototype/Textures/T_PaperFolded_V2_D.uasset`
  - `Content/Prototype/Textures/T_PaperOld_V2_D.uasset`
- 적용: `Scripts/Prepare-AIArt.ps1`이 2×2 시트를 네 장으로 분리하고,
  `Scripts/create_textured_materials.py`가 `M_PaperClean`, `M_PaperOld`
  머티리얼에 V2 텍스처를 연결합니다. 젖은 종이·접힌 종이 재질은 쓰는 곳이
  없어 2026-09-30에 뺐고, 두 텍스처는 인쇄 아틀라스 자리로만 남아 있습니다.
- 이전 V1 시트(`SheetPaper.png`)는 종이가 아니라 새우 과자 봉지 그림이었습니다.
  2026-09-30에 파생 텍스처와 함께 지웠습니다.

사용한 프롬프트 전문:

```text
[공통 스타일 규칙 - 반드시 지켜줘] 이건 사실적인 PBR 렌더링 기반 1인칭 공포게임에 쓸 종이 텍스처야. 여러 장을 따로 만들어도 전부 같은 세계에 있는 것처럼 보여야 해. 실제 종이를 스튜디오에서 완전 정면으로 촬영한 팩샷 사진처럼 만들어줘. 대형 소프트박스 조명, 그림자 거의 없음, 원근 왜곡 없이 완전 정면 평면. 종이의 섬유결, 눌림, 습기와 세월의 물성이 섬세하게 보여야 해. 배경은 순백색이고 색은 채도를 낮춘 실제 재료 톤이어야 해. 실존 상표나 로고는 절대 금지. 이번 이미지는 Unreal Engine 5 생활공포게임의 문서 배경용 원본 시트다. 전체 캔버스는 정확한 1:1 정사각형, 2열×2행의 엄격한 격자. 각 칸 사이는 굵고 깨끗한 흰 여백으로 완전히 분리하고, 네 칸의 조명·카메라·그레인·노출은 완전히 동일하게 유지한다. 각 칸 중앙에 세로 A4 비율(1:1.414)의 빈 종이 한 장을 완전 정면으로 놓고 셀 높이의 약 90%를 채운다. 좌상단: 깨끗하지만 아주 약간 따뜻한 흰색 복사용지, 미세한 종이 섬유만. 우상단: 아래 가장자리에서 번진 현실적인 물얼룩과 옅은 조수선, 살짝 물결친 가장자리의 젖은 종이. 좌하단: 한 번 세로로, 한 번 가로로 접었다 펼친 부드러운 십자 접힘 자국이 있는 종이. 우하단: 누렇게 바랜 낡은 종이, 모서리 마모와 아주 약한 갈색 반점, 찢어지거나 구멍 나지 않음. 네 종이 모두 완전히 비어 있어야 한다. 글자, 숫자, 도장, 선, 그림, 로고, 워터마크, 테이프, 클립, 손, 소품을 절대 넣지 말 것. 종이 밖 배경에는 그림자, 그라데이션, 바닥면, 반사, 질감이 없어야 한다. 게임에서 한국어 문자를 런타임으로 올릴 예정이므로 넓고 깨끗한 중앙 여백을 유지한다.
```

### 「없는 층」 환경·인물·흔적·핵심 소품 생성 기록

- 서비스: OpenAI ImageGen 내장 도구
- 생성일: 2026-08-10
- 보존 원본: `SheetMissingFloorEnvironmentReference.png`,
  `SheetListenerEntityAnatomyReference.png`, `TextureMissingFloorDryPlaster.png`,
  `SheetMissingFloorResidueMasks.png`, `SheetMissingFloorDistantCharacters.png`,
  `SheetMissingFloorHeroPropsReference.png`, `ListenerEntityFrontCutout.png`,
  `SheetListenerEntityCrawlPhases.png`
- SHA-256:
  - 환경 `041295A5CAC88B7180B58E8E0106DC0C62282B61FE48F9DFD2D3F6A912729AD8`
  - 인체 `0C327F322224173879F1D19872A4F79F5CE725295155BBB60C337295B50823D7`
  - 석고 `35D842AF7D9BE620E9CB22D10A8D60E2A2189CED72283B2EF848226C199DFDC3`
  - 흔적 `201B650D2B052F26F8AE8961D17A681C249721E99CF01862B2922F3817FDA4D7`
  - 인물 `0E8879B58588798184FCFC406348154E2A3AB33E5D88E26259A40DFC348633C1`
  - 소품 `DEB5620CBDAC8C0CCE6EC782613077A76DB6EFFCB4522E380E1B48E08869DC84`
  - 정면 인체 `80B4CDD471BD163E424E5291C0EDD03B4A0E9E9D376C4BD330FFF4FA47A50338`
  - 기어오기 4단계 `C8DAE7F12E5C047ED277BC556E4043F2052321359ED628CE534267C2F35CAD8A`
- 처리: `Prepare-AIArt.ps1`이 흔적·인물·동작 시트를 분리하고 마젠타/초록 키를
  알파로 변환한다. `generate_ai_pbr_maps.py`가 석고와 정면 인체 5장 각각의
  N/R/A를 만들며,
  `generate_meshes.py`는 인체와 네 핵심 소품을 실제 치수의 3D 메시로
  재구성한다. `create_textured_materials.py`가 PBR·masked 머티리얼을 만든다.
- 적용 경계: 환경 시트는 방향 기준, 건식 석고는 PBR, 흔적은 값 마스크,
  위층 사람과 근접 소품은 3D다. 위층 사람 정면 레이어는 3D 접지 셸을
  보존한 정면 LOD다. 1.6m에서 켜고 1.25m까지 히스테리시스로 유지하며,
  측면에서는 반드시 숨긴다.
  나머지 스프라이트는 원거리 접근 불가 인물만 허용한다.
- 전체 프롬프트: `Docs/IMAGEGEN_PROMPTS_2026-08-10.md`
- 배치·거리·LOD 합격표: `Docs/MISSING_FLOOR_ART_MATRIX.md`

### 발소리 표면·현관문 생성 기록

- 서비스: 로컬 ChatGPT 소프트웨어의 ImageGen (프로젝트 동일 작업공간)
- 생성일: 2026-08-14
- 보존 원본: `TextureVillaStairCheckerPlatePaintedSteel.png`,
  `TextureRooftopUrethaneWaterproofing.png`,
  `TextureApartmentEntranceDoorCharcoalSteel.png`
- 파생: 알베도 3장과 PBR 동반 채널 9장, 머티리얼 3종. 같이 만든 석고 파편 바닥·
  계량기 문자판·먹지는 게임이 쓰지 않아 2026-09-30에 지웠다
- 신규 파이프라인 단계: `Scripts/condition_ai_tiles.py`. ImageGen 스캔은
  타일이 될 수 없다 — 생성기는 구도를 만들고, 그 저주파 밝기 얼룩은 타일
  격자마다 반복되어 조명을 구운 것으로 읽힌다(`MISSING_FLOOR_ART_MATRIX.md`
  원칙 4가 금지하는 것). 이 단계가 채널별 flat-field로 얼룩과 색 캐스트를
  함께 지우고, 규칙 패턴은 주기를 찾아 정수배로 자른 뒤 좁은 크로스페이드로,
  확률적 표면은 넓은 크로스페이드로 이음매를 없앤다. **`generate_ai_pbr_maps.py`
  보다 먼저 돌아야 한다** — 노멀이 알베도에서 유도되므로 남은 얼룩은 가짜
  기하가 된다.
- 게인의 근거: 점묘 억제 프롬프트가 진짜 요철도 함께 눌러서 알베도 표준편차가
  1.86~3.78로 왔다(승인 에셋 대역 10~12). 노멀에 다이아몬드가 남지 않아
  `detail_gain`으로 되살렸고, 얼룩 판정은 절대 편차가 아니라 대비 대비
  편차로 잰다.
- 검증: 아트 계약(raw 56·scans 20·pbr_maps 80), `ART_TARGETED_BUILD PASS
  target=MissingFloor assets=71 uasset_audit=1`, V5 8지점 전부 밴드 안,
  `MISSINGFLOOR_GREYBOX PASS`, `Validate-Project.ps1` 전 계약 무회귀.
- 승인 경계: 세 바닥이 실제 플레이에서 **눈으로 구분되는지**는 사람의
  판단이다. 계약은 프레임이 깨지지 않았다는 것까지만 말한다.
- 전체 프롬프트: `Docs/IMAGEGEN_PROMPTS_2026-08-14.md`

### M0 1인칭 두드리기 손 생성 기록

- 서비스/모드: OpenAI ImageGen 내장 도구, `stylized-concept`
- 생성일: 2026-08-11
- 보존 원본: `Content/SourceArt/AI/SheetFirstPersonKnockPhases_v1.png`
- 투명 마스터: `Content/SourceArt/AI/SheetFirstPersonKnockPhases_v1_RGBA.png`
- 원본 SHA-256:
  `36C3E5BA1829C36D0B8CE967DFEF5719768E3AC02ABD444F088437F448DD58E1`
- 승인 v2 원본/투명 마스터:
  `Content/SourceArt/AI/SheetFirstPersonKnockPhases_v2.png`,
  `Content/SourceArt/AI/SheetFirstPersonKnockPhases_v2_RGBA.png`
- 승인 v2 SHA-256:
  `412D9173E7EE8AFD2270CF45008E3904AC0530DF2D0CDFCFEBCBC3A6DD9241E7`
- 파생: `Content/SourceArt/T_FPHandKnock0_D.png`부터
  `T_FPHandKnock3_D.png`, 런타임 `/Game/Prototype/Textures/T_FPHandKnock0_D`
  부터 `T_FPHandKnock3_D`
- 선택: v1 런타임 캡처에서 소매가 화면 안쪽 직선으로 끝나 v2 편집으로 같은
  손·네 포즈·세 땀을 유지한 채 소매를 각 셀 우하단 모서리까지 연장했다.
- 처리: ImageGen의 균일 초록 배경을 공식 `remove_chroma_key.py`의
  border auto-key·soft matte·despill·1px edge contract로 RGBA화했다.
  `Prepare-AIArt.ps1`이 2×2 시트를 투명 여백을 포함한 768px 네 장으로
  분리하고 알파를 보존한 제한적 2차 green despill을 적용한다. 이 여백은
  소매가 화면 안쪽의 사각 경계에서 잘리지 않게 타일 끝을 화면 밖으로 보낸다.
  `generate_surface_textures.py`는
  UI group·NoMip·Clamp·NeverStream으로 임포트한다.
- 적용 경계: 유효 노크의 준비·접촉·반동만 카메라 UI-space에서 알파
  블렌딩한다. 기존 3D/PBR 세계, 충돌, 조명과 접촉 그림자는 교체하지 않는다.
  흔들림 감소는 접촉 정지 프레임을 사용한다.
- 프롬프트 전문: `Docs/IMAGEGEN_PROMPTS_2026-08-11.md`

### 사용을 중단한 포획 이미지

2026-08-11에 만든 `SheetListenerCaptureEmbracePhases_v1`과 파생 텍스처
`T_FPCaptureEmbrace0_D`~`T_FPCaptureEmbrace3_D`는 현재 게임에서 쓰지 않는다.
포획 장면은 3D 캐릭터의 몸과 팔로 보여 준다. 코드와 다른 에셋에서 참조하지
않는 것을 확인하고 2026-09-22에 원본·투명본·파생 이미지·에셋 10개와 빌드
목록을 정리했다. 생성 당시 기록은 커밋 `b3da61a`의 이 문서와
`Docs/IMAGEGEN_PROMPTS_2026-08-11.md`에 남아 있다.

## 2026-08-11 없는 층 M5 공동 리빌 원본

- `Content/SourceArt/AI/SheetFinalCavityRemainsReference_v1.png`
  - 도구/모드: OpenAI ImageGen 내장 생성, `stylized-concept`
  - SHA-256: `950EA1404E6804C083F4EF060ABA7AE175EB70DAD9BE396333605E889C855297`
  - 용도: 건조한 부분 골격, 내려앉은 작업복, 방수포와 캐스터의 3D 비율·재질 참고.
    생성본의 피부·머리카락과 스튜디오 배경은 사용하지 않는다.
  - 런타임: 4개 Geometry Script 정적 메시로 재구성하며 원본 PNG를 카메라나
    재질에 직접 노출하지 않는다.
- `Content/SourceArt/AI/SheetMokHansooConfrontationReference_v1.png`
  - 도구/모드: OpenAI ImageGen 내장 생성, `stylized-concept`
  - SHA-256: `466FC1AF73CD8852E955022FA5D9FBE1F38A04F6623F318F966F0373FEBCF618`
  - 용도: 목한수의 평균 체형, 작업복, 두 손 석고보드 파지와 피로한 표정 참고.
  - 런타임: 작업복·머리/손·석고보드 3개 근접 3D 메시로 재구성한다. 기존
    `T_SpriteMok_D`는 12m 이상 접근 불가 원거리 규칙을 유지한다.
- `Content/SourceArt/AI/FinalCavityFrontBlend_v1.png`
  - 도구/모드: OpenAI ImageGen 내장 생성·편집, `game-asset`
  - SHA-256: `9C26AAC9D3160CBD73B3183A332BC822FA8B6A1B655D5B24A6D642E1952B6D11`
  - 용도: 절차 3D 셸에서 부족한 건조한 의복·골격의 정면 판독 정보.
  - 파생: `T_SpriteFinalCavity_{D,N,R,A}`와 `M_SpriteFinalCavity`. 105~360cm·
    정면 내적 0.68 초과에서만 보이며, 숨은 3D 셸의 실제 그림자를 유지한다.
- `Content/SourceArt/AI/MokHansooFinalFrontBlend_v1.png`
  - 도구/모드: OpenAI ImageGen 내장 생성, `game-asset`
  - SHA-256: `B2C6787D66E595F6A32BD6FD653060773097BF560CFE0EEDF0877EAB6FA6547C`
  - 용도: 목한수의 피로한 얼굴과 낡은 재킷을 근접 정면에서 판독하기 위한 보강.
  - 파생: `T_SpriteMokFinalUpper_{D,N,R,A}`와 `M_SpriteMokFinalUpper`. 세로
    31~39%에서 알파를 없애 실제 3D 석고보드·하체·접지·그림자를 보존한다.

네 원본 모두 프로젝트 제작 레퍼런스·파생 소스이며 외부 인물·브랜드·상표를
참조하지 않은 생성물이다. 최종 런타임 형상과 배치, 거리·각도·재질 선택은
프로젝트 코드가 소유한다. 전체 프롬프트와 생성·편집 이력은
`Docs/IMAGEGEN_PROMPTS_2026-08-11.md`에 보존한다.

## 2026-09-17 관리실 문구류

- `BoothStationeryStudy_20260917.png`: 알파의 근영사 장부 제품 사진과 STAEDTLER 노리스 연필 사진을 형태 참고로 사용한 내장 imagegen 생성물. 외부 사진은 제품 형태를 확인하는 데 쓰며 게임에 원본을 넣지 않는다.
- `ComplaintRubbing_20260917.png`: 글자 없는 흑연 문지름 질감. 실제 한글은 별도 마스크로 합성한다.
- 두 생성 원본과 최종 프롬프트는 `Content/SourceArt/AI/BoothPrompts_20260917.json`에 연결되어 있다.
- 접수철의 손글씨는 나눔손글씨 펜체를 사용한다. [Google Fonts 원본](https://github.com/google/fonts/blob/main/ofl/nanumpenscript/METADATA.pb), 글꼴 파일과 SIL OFL 고지는 `Scripts/fonts/NanumPenScript/`에 보존한다. 글꼴을 수정하지 않았으며 게임에서는 구운 인쇄 이미지를 사용한다.
- 실물 참고 링크, 모델 치수, 전후 화면은 [관리실 점검 기록](BOOTH_REVIEW_20260917.md)에 정리한다.

## 2026-09-17 로비 분전반

- `CircuitPanelReference_20260917.png`: LS EBS32Fb 제품 실물 사진을 구조 참고로 전달해 내장 imagegen으로 만든 정면·사선·확대 시트다. 사진의 상표나 인증 표시는 모델에 옮기지 않았다.
- `CorridorCabinetReference_20260917.png`: KDM의 매입형 금속 함 사진과 로비 분전반 시트를 함께 참고해 만든 닫힌 함체 시트다. 별도 JSON에 프롬프트를 보존한다. `SM_CorridorCircuitCabinet`의 문틈·힌지·잠금쇠와 매입 깊이에 사용했다. 한글 이름표는 `SM_CorridorCircuitPrint`로 분리했다.
- 참고 사진 주소와 사용한 프롬프트는 `Content/SourceArt/AI/CircuitPanelReference_20260917.json`에 남긴다. 판매 사진 원본은 배포 에셋에 포함하지 않는다.
- Blender에서 `SM_LobbyCircuitPanel`, `SM_CircuitToggle`, `SM_CircuitPanelPrints`로 제작했다. 한글 이름표는 `Scripts/build_circuit_prints.py`로 만들고 인쇄 UV만 보정했다.
- 전원 상태, 회전축, 그림자와 반사는 게임이 계산한다. 생성 참고 이미지를 분전반 앞면에 붙이지 않는다.
- 실제 게임 화면과 검증 결과는 [분전반 점검 기록](CIRCUIT_REVIEW_20260917.md)에 정리한다.

프로젝트 코드의 공개 라이선스는 저장소 소유자가 별도로 선택합니다. 선택 전까지
저작권 고지만으로 공개 사용 권한을 추정하지 않습니다.

## 2026-08-12 타이틀·엔딩 C 환경 키아트

- 도구/모드: OpenAI 내장 ImageGen, `stylized-concept`
- 보존 원본: `Content/SourceArt/AI/TitleBackgroundMissingFloor_v1.png`
- 원본 SHA-256:
  `4831357AA6439F9CF93CC3D5CC4664DDF8EB995D59759E1F319504246CD57D39`
- 파생: `Content/SourceArt/T_TitleBackground_D.png`, 1920×1080 RGBA
- 런타임: `/Game/UI/Textures/T_TitleBackground_D`, `TEXTUREGROUP_UI`,
  `NoMipmaps`, `Clamp`, `NeverStream`
- 권리/참조: 외부 작품·로고·실존 건물·인물을 입력하지 않은 환경 생성물이다.
  UI 글자와 브랜드는 포함하지 않는다.
- 적용 경계: 타이틀/타이틀에서 연 크레딧의 배경과 엔딩 C의 무브랜드 매물
  외관 크롭만 담당한다. 방 번호·매물명·후기는 네이티브 한글 UI가 소유한다.
  일시정지는 현재 월드 화면을 유지하고, 메뉴·포커스·현지화 문자는 C++ HUD가
  소유한다.
- 프롬프트 전문: `Docs/IMAGEGEN_PROMPTS_2026-08-12.md`

## 2026-08-18 한국 빌라 외벽 스터코

- 도구/모드: OpenAI 내장 ImageGen, `game-asset`
- 보존 원본: `Content/SourceArt/AI/TextureKoreanVillaStucco_v1.png`
- 원본 SHA-256:
  `474A8D005146B499FDB39CBAE62FC17F790EC71E9D833E4864A387DBA959BA57`
- 파생: `Content/SourceArt/T_KoreanVillaStucco_{D,N,R,A}.png`
- 런타임: `/Game/Prototype/Textures/T_KoreanVillaStucco_{D,N,R,A}`와
  `/Game/Prototype/Materials/M_VillaStucco_{X,Y}`
- 적용 경계: 생성물은 한글·상표·사물·조명·그림자가 없는 외벽 BaseColor
  원본만 담당한다. N/R/A와 월드 매핑은 재현 가능한 로컬 스크립트가 만들며,
  UE의 실제 광원과 Lumen이 최종 명암을 계산한다.
- 프롬프트 전문: `Docs/IMAGEGEN_PROMPTS_2026-08-18.md`

## 2026-09-28 옥상 밤 원경

- 도구/모드: gpt-image 스킬(Codex 내장 image_gen, ChatGPT 구독 인증), 배경 투명
- 보존 원본과 SHA-256:
  - `Content/SourceArt/AI/SkylineNorth_20260928.png`
    `D38CB1F2756CBD7DAFFF73428D5FAE50B44C5D6C2EF3622A02E650093FA17941`
  - `Content/SourceArt/AI/SkylineEast_20260928.png`
    `869320E7CA90DED6E080C119D8985202B53A104665D9BCE8146ED4CDDDC0FF30`
  - `Content/SourceArt/AI/SkylineSouth_20260928.png`
    `2E5649EE54FDAA7C7DFAEB56FBE2C560B251EEEC65FC7E5C075B5CBCE546C44F`
  - `Content/SourceArt/AI/SkylineWest_20260928.png`
    `895E62F1809A7E6D805CC7613D9882903B42B5ACA2C15EB65ED0C36D311BA511`
- 파생: `Content/SourceArt/NightView/NightSkyline_D.png`(네 장을 북·동·남·서 순서로
  쌓은 1536×4096 RGBA 띠)와 `NightSkyline_M.png`(불빛 세기·창 문턱·종류 마스크).
  `Scripts/build_night_view_masks.py`가 만든다. 같은 스크립트가 2026-09-16 창밖 사진의
  마스크 `ApartmentNightVista_M.png`도 만든다
- 런타임: `/Game/Prototype/Textures/T_NightSkyline_{D,M}`, `T_ApartmentNightVista_M`,
  `/Game/Prototype/Materials/M_NightSkyline`, `M_NightSkyGlow`. 원경 텍스처는 재질이
  시선으로 좌표를 계산하므로 스트리밍하지 않는다
- 권리/참조: 참고 사진 없이 문장만으로 만든 가상의 동네다. 사람·차·읽을 수 있는
  글자·실존 상호가 없다
- 적용 경계: 옥상에서 보이는 접근할 수 없는 원경만 담당한다. 가까운 난간·물탱크·
  이웃 빌라는 3D 지오메트리이고, 창이 켜지고 꺼지는 시간은 게임이 정한다
- 프롬프트 전문: `Docs/IMAGEGEN_PROMPTS_2026-09-28.md`, 원본마다 같은 이름의 JSON

## 2026-09-28 5층 옥탑 외벽 패널

- 도구/모드: gpt-image 스킬(Codex 내장 image_gen, ChatGPT 구독 인증)
- 보존 원본: `Content/SourceArt/AI/AnnexSandwichPanel_20260928.png`
  `E6BAD9852811CB0F418EF3F7697EEEBAD37B3726AD604908ED871645DBBEA7DF`
- 파생: `Content/SourceArt/AnnexPanel/AnnexSandwichPanel_D.png`. `Scripts/build_annex_panel_texture.py`가
  위아래로도 이어지게 다듬는다
- 런타임: `/Game/Prototype/Textures/T_AnnexSandwichPanel_D`, `/Game/Prototype/Materials/M_AnnexPanel`
  (가로 200 cm·세로 120 cm 한 칸, 옥상 바닥 위 30 cm에 빗물 튄 때)
- 권리/참조: 참고 사진 없이 문장만으로 만든 가상의 도장 강판이다. 글자·상표가 없다
- 적용 경계: 5층 증축부 바깥 외피와 후레싱만 담당한다. 안쪽 석고와 문, 충돌은 기존 벽이
  그대로 맡는다
- 프롬프트 전문: `Docs/IMAGEGEN_PROMPTS_2026-09-28.md`, 원본 옆 JSON

## 2026-09-28 골목 이웃 창 뒤의 방

- 도구/모드: gpt-image 스킬(Codex 내장 image_gen, ChatGPT 구독 인증)
- 보존 원본과 SHA-256:
  - `Content/SourceArt/AI/RoomInteriorLamp_20260928.png`
    `941BDCEF303A40F048B67180EAABF86CB663F2655CD3E019DA77EB6128B78E07`
  - `Content/SourceArt/AI/RoomInteriorTv_20260928.png`
    `6B536D6625CD29107EC7221EEE397A15914AD6AC34B6C6716AB15D913466F323`
  - `Content/SourceArt/AI/RoomInteriorKitchen_20260928.png`
    `B6FA42275AAEF41CFF085FA8174773C6BD19DA75A1C794DCFD08694B0FA8615F`
  - `Content/SourceArt/AI/RoomInteriorStudy_20260928.png`
    `236BC45D2BCD92967789414C6F6920D89E9DFE5038ABDC538385A954CCF48040`
- 파생: `Content/SourceArt/RoomInterior/RoomInteriors_D.png`(2x2, 2048). `Scripts/build_room_interior_atlas.py`가 만든다
- 런타임: `/Game/Prototype/Textures/T_RoomInteriors_D`, `/Game/Prototype/Materials/M_RoomInterior`. 좌표를 재질이
  시선으로 계산하므로 스트리밍하지 않는다
- 권리/참조: 참고 사진 없이 문장만으로 만든 가상의 방이다. 사람·읽을 수 있는 글자·상표가 없다
- 적용 경계: 골목 이웃 건물의 창 여덟 개 뒤에서만 보인다. 들어갈 수 없는 방이고, 빌라 자기 앞면 창과
  403호·복도 창은 쓰지 않는다
- 프롬프트 전문: `Docs/IMAGEGEN_PROMPTS_2026-09-28.md`, 원본마다 같은 이름의 JSON

## 2026-09-29 현관문 인쇄물과 공용부 때

- 도구/모드: gpt-image 스킬(Codex 내장 image_gen, ChatGPT 구독 인증)
- 보존 원본과 SHA-256:
  - `Content/SourceArt/AI/DoorFlyerChinese_20260929.png`
    `3DA9E088D0707B9563A458F59E6AEE7F673B6ABF8A88114B7FA5E6C957CC71A0`
  - `Content/SourceArt/AI/DoorFlyerRealty_20260929.png`
    `D92907E3266A57C96F238432ABA9C7B9A54CEE972E68D7869CCA2F7783B331EE`
  - `Content/SourceArt/AI/DoorGasSticker_20260929.png`
    `958088E6E04E20D5AC29408274E93B1F1194506D102746AE30C541440E9F1F80`
  - `Content/SourceArt/AI/DoorKeySticker_20260929.png`
    `03631DCB257E061A29FCCDF25111398474C470C3DCF40CF5A6F12DE50CF8EE12`
  - `Content/SourceArt/AI/DoorNoFlyer_20260929.png`
    `026A68B29527CBBC4CE581645C5BE6E1060D0A7236E2C152ED5CE9B44A2F2B8C`
  - `Content/SourceArt/AI/GrimeCeiling_20260929.png`
    `0DFBAAC04449C36659B5334E8F9E7BA1D8990F33006A03BE46C0B25FA0EF6BD2`
  - `Content/SourceArt/AI/GrimeFloor_20260929.png`
    `5BAA3889E0326F74496BDA7DAA167A0E7FE780DDAA340B4251219BECA76B9846`
  - `Content/SourceArt/AI/GrimeWall_20260929.png`
    `856B3475A1ED036F3E48D2C96E24138E6D1D60FE8BFC61E3674BF30DA8ADB126`
- 파생: 현관문 인쇄물은 `Scripts/blender/build_door_prints.py`가 401호·402호 문마다
  한 장씩 모아 `SM_DoorPrints401`·`SM_DoorPrints402`와 구운 D/N/ORM을 만든다.
  때 셋은 `Scripts/build_grime_masks.py`가 잔점을 걷고 이음매 없이 다듬어
  `Content/SourceArt/Grime/*_M.png` → `T_WallGrime_M`·`T_FloorGrime_M`·`T_CeilingStain_M`이 된다
- 런타임: 인쇄물 메시는 그림자와 거리장 조명을 끄고 9m에서 컬링한다. 때는 공용부 벽·바닥·천장
  재질이 월드 좌표로 곱하는 회색 마스크라 드로우콜이 늘지 않는다
- 권리/참조: 상호·전화번호는 모두 가상이다. 번호는 국번이 0으로 시작해 실제로 걸리지 않는다.
  사람·실제 상표·읽을 수 없는 가짜 글자가 없다
- 프롬프트 전문: 원본마다 같은 이름의 JSON

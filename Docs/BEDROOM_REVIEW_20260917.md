# 403호 침구와 책상 검수

2026년 9월 17일 작업. 9월 16일까지 공개된 자료와 실물 사진을 참고했다. 현재 게임의 입주 경로로 들어가 침대, 벽, 책상을 여섯 시점에서 촬영했다.

9월 18일에 창틀과 머리판이 겹친다는 제보를 확인해 침대를 12cm 옮겼다. 아래 수정 후 캡처 여섯 장도 다시 촬영했다. 이 문서의 성능 수치는 17일 측정값이다. 17일 검사 기록의 캡처 해시는 당시 커밋 `b44bede`의 파일에 해당한다.

## 침대

기존 침구는 각진 상자 세 개였고 베개가 없었다. [IKEA 면 침구 사진](https://www.ikea.co.id/en/products/bedlinen/duvet-covers/angslilja-art-60592773)과 [실제 베개 사진](https://www.ikea.com/kr/ko/images/products/pilspinnare-pillow-high-side-back-sleeper__1300769_pe937182_s5.jpg)을 imagegen에 넣어 다각도 참고도와 면 원단을 만들었다. Blender에서 매트리스, 베개, 접힌 이불을 모델링하고 원단을 구웠다.

| 수정 전 | 수정 후 |
|---|---|
| ![상자를 쌓은 침구와 검게 끊긴 벽 얼룩](Media/readme/bedroom-before-bed.webp) | ![베개와 늘어진 이불, 벽지가 이어지는 벽면](Media/readme/bedroom-after-bed.webp) |

위 사진은 같은 위치에서 찍은 게임 화면이다. [생성 참고도](../Content/SourceArt/AI/BeddingStudy_20260917.png), [원단](../Content/SourceArt/AI/BeddingCotton_20260917.png)을 따로 보관했다. 생성 이미지를 완성 화면으로 제시하지 않았다.

매트리스는 94×194×18cm, 베개는 70×45cm다. 이불의 옆면은 천 계산을 60프레임 돌려 늘어뜨렸고, 벽 쪽 가장자리는 3cm 틈 안으로 접었다. 첫 반입 때 베개가 납작해 보여 속이 찬 형태로 다시 고쳤다. [베개 근접 화면](Media/bedroom-after-pillow.png)과 [이불 끝 근접 화면](Media/bedroom-after-hem.png)에서 모서리와 두께를 확인했다.

침대를 조사할 수 있는 범위도 베개 한쪽에서 침구 전체로 넓혔다. 프레임의 단순 충돌이 빈 공간까지 감싸 시선을 막던 문제를 고쳤다. 몸을 막는 충돌과 조사 판정을 나눠, 옆과 발치에서 이불을 바라봐도 ‘눕는다’가 잡힌다. 입주 조사가 끝나기 전에는 사용할 수 없고, 1.2초를 채우지 않고 놓으면 잠들지 않는다.

## 벽과 책상

벽 얼룩은 마스크를 일정 농도에서 잘라 그린 불투명한 평면이었다. 그래서 얇은 자국도 검게 보였고 그림의 사각 경계가 드러났다. [UE 5.8 데칼 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/decal-materials-in-unreal-engine)를 참고해 벽 위에 색만 옅게 투영하도록 바꿨다. 벽지의 꽃무늬와 요철은 남기고, 네 가장자리에서 농도를 줄였다. 깊이는 벽에만 닿게 제한했다.

연필꽂이는 입구가 막힌 원통이었고 연필 끝도 평평했다. [J.Burrows 제품 사진](https://s3-ap-southeast-2.amazonaws.com/wc-prod-pim/JPEG_1000x1000/JBPCUPBK2_j_burrows_pen_cup_black.jpg)을 보고 입구, 얇은 벽, 안쪽 바닥을 만들었다. 관리실의 육각 연필을 재사용해 나무와 흑연 끝을 붙이고 길이와 기울기를 달리했다. 새 문구류는 상판의 실제 높이에 맞춰 놓았다.

| 수정 전 | 수정 후 |
|---|---|
| ![막힌 연필꽂이와 굵은 막대 모양 연필](Media/readme/bedroom-before-desk.webp) | ![열린 연필꽂이와 가늘게 깎인 연필](Media/readme/bedroom-after-desk.webp) |

## 성능

[UE 5.8 LOD 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/static-mesh-automatic-lod-generation-in-unreal-engine)와 [가시성·가림 판정 문서](https://dev.epicgames.com/documentation/unreal-engine/visibility-and-occlusion-culling-in-unreal-engine)를 대조했다. 침구와 문구류는 각각 한 메시·한 재질로 묶고 4단계 LOD를 반입했다. 천 계산은 제작 중에만 돌린다. 게임에서는 고정 메시와 단순 충돌을 사용한다. 작은 문구류는 8.5m에서 숨기고 벽 얼룩은 화면에서 작아지면 사라진다.

새 침구는 약 8,500개, 문구류는 1,046개 삼각형이다. 침구의 텍스처는 2K, 문구류는 512px이며 모두 스트리밍한다. 침구와 문구류를 그리는 컴포넌트는 합계 7개에서 2개로 줄었다. 전체 장면의 드로 콜까지 줄었다는 뜻은 아니다. 데칼 투영과 늘어난 형상도 비용이 든다.

Ryzen 9 7900X·RTX 3060 PC에서 1920×1080, 높음, 화면 비율 100%, D3D12, VSync와 프레임 제한을 끄고 같은 여섯 시점을 측정했다. 초기 120프레임을 제외했다.

| 항목 | 수정 전 | 수정 후 |
|---|---:|---:|
| 분석 프레임 수 | 1,511 | 1,567 |
| 프레임 시간 중앙값 | 17.859ms | 17.319ms |
| 프레임 시간 p95 | 19.578ms | 18.875ms |
| GPU 시간 중앙값 | 17.321ms | 16.854ms |
| 전체 드로 콜 중앙값 | 496 | 514 |
| GPU 메모리 중앙값 | 2,957.9MiB | 2,957.5MiB |
| 33.33ms 초과 프레임 | 0 | 0 |

각각 한 번씩 측정한 결과이므로 작은 차이를 확정적인 성능 향상으로 보지는 않는다. 이 설정에서 60fps 고정을 달성했다고 할 수도 없다. [수정 전 원본·요약](Performance/Bedroom20260917-BeforeHigh/)과 [수정 후 원본·요약](Performance/Bedroom20260917-AfterHigh/)을 보관했다.

## 진행과 안내 화면

[9월 16일 공개된 Townfall 제작진 글](https://blog.playstation.com/2026/09/16/silent-hill-townfall-creators-break-down-ps5-features-out-september-25/)에서 생활 공간과 제한된 시야, 소리로 긴장을 만드는 방식을 참고했다. 이번에는 새 퍼즐이나 설명 대사를 덧붙이지 않고, 방을 조사하고 잠드는 기존 흐름에서 시선과 조작을 방해하던 부분을 고쳤다. 이야기 전체를 새로 썼거나 공포의 강도를 검증한 작업은 아니다.

침실 시각 검수와 상호작용 검사는 `Scripts/Run-BedroomReview.ps1`로 다시 돌릴 수 있다. `-Measure -Profile AfterHigh`를 붙이면 같은 시점의 성능 CSV를 저장한다. `-Before`는 수정 전 코드에서 촬영할 때 파일 이름을 구분하는 옵션이며, 예전 에셋으로 되돌리는 기능은 아니다.

안내가 자동으로 사라지고 키보드·패드로 다시 열리는지, 문서나 일시정지 메뉴와 겹치지 않는지 다시 검사했다. [포획 후 방으로 돌아오는 연속 화면](Media/ux-capture-recovery-1920x1080.gif)도 새로 촬영했다. 검증 결과는 [검사 기록](Performance/Bedroom20260917-checks.txt)에 남겼다.

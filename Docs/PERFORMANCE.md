# Windows 출시 성능·지원 계약

문서 버전: `Windows-v1`

기준일: `2026-08-06`

2026-10-08의 Shipping 성능 측정과 화면·진행 오류 수정은
[성능·화면·게임 진행 점검](OPTIMIZATION_REVIEW_2026-10-08.md)에 기록했다.

2026-09-14 모델·입력 수정 뒤 현재 장비에서 확인한 Development 실행 결과는
[실체감 수정·검증 기록](REALISM_REVIEW_2026-09-14.md)에 있다. 이 문서의
Shipping 출시 사양과 여러 장비의 합격선은 그대로 적용한다.

지원 대상: `IndieGame Win64 Shipping`

이 문서는 Windows 첫 출시에서 바꾸지 않을 지원 범위와 성능 합격선을
정한다. 아래 수치는 **출시 목표**이며 측정 결과가 아니다. 현재 SHA의
Shipping 후보를 지정된 장비에서 측정하고 원본 증거를 남기기 전까지
성능 게이트는 `BLOCKED`다. 목표표를 작성했다는 사실을 PASS로 해석하지
않는다.

지원 범위나 합격선을 바꾸려면 문서 버전을 올리고 G5 화면 검증과 G6
성능·패키지 검증을 새 후보로 다시 실행한다. 결과가 기준에 미달하면
사양이나 수치를 조용히 낮추지 않고 최적화, 지원 범위 변경, 출시 차단 중
하나를 이슈로 결정한다.

## 출시 사양

| 구분 | 최소 사양 | 권장 사양 |
|---|---|---|
| 운영체제 | Windows 10 22H2 또는 Windows 11 Home/Pro 25H2, 64비트 | Windows 11 Home/Pro 25H2, 64비트 |
| CPU | Intel Core i5-8400 또는 AMD Ryzen 5 2600 | Intel Core i5-12400 또는 AMD Ryzen 5 5600 |
| GPU | NVIDIA GeForce GTX 1060 6GB 또는 AMD Radeon RX 580 8GB, DirectX 12 | NVIDIA GeForce RTX 2060 6GB 또는 AMD Radeon RX 6600 8GB, DirectX 12 |
| 시스템 메모리 | 16GB | 16GB 이상 |
| 저장 장치 | SSD 필수, 설치 전 여유 공간 12GB 이상 | SSD 필수, 설치 전 여유 공간 12GB 이상 |
| 기본 성능 목표 | 1920×1080, Low, 30fps | 1920×1080, High, 60fps |

동급 장비는 CPU 코어 구성, GPU 전용 메모리와 DirectX 12 지원이 표의
기준보다 낮지 않을 때만 같은 등급으로 취급한다. 내장 GPU, HDD, ARM
Windows, Steam Deck/Linux 호환 계층은 Windows-v1 지원 범위가 아니다.

### 필수 장비 축

표에서 `또는`으로 지원한다고 쓴 OS·CPU·GPU 계열을 한 조합의 결과로
대신하지 않는다. Windows-v1 PASS에는 최소 다음 여섯 장비가 필요하다.
CPU와 GPU는 표의 정확한 모델 또는 사전 등록한 동급 모델을 쓰며, 동급
판정 근거를 결과 매니페스트에 남긴다.

| 장비 ID | OS | CPU 등급 | GPU 계열 | 필수 성능 경로 |
|---|---|---|---|---|
| `MIN-W10-NV` | Windows 10 22H2, 최신 누적 업데이트 | 최소 Intel | NVIDIA GTX 1060 6GB급 | 720p Low 30, 1080p Low 30 |
| `MIN-W10-AMD` | Windows 10 22H2, 최신 누적 업데이트 | 최소 AMD | AMD RX 580 8GB급 | 720p Low 30, 1080p Low 30 |
| `MIN-W11-NV` | Windows 11 Home/Pro 25H2, 최신 누적 업데이트 | 최소 Intel | NVIDIA GTX 1060 6GB급 | 720p Low 30, 1080p Low 30 |
| `MIN-W11-AMD` | Windows 11 Home/Pro 25H2, 최신 누적 업데이트 | 최소 AMD | AMD RX 580 8GB급 | 720p Low 30, 1080p Low 30 |
| `REC-W11-NV` | Windows 11 Home/Pro 25H2, 최신 누적 업데이트 | 권장 Intel | NVIDIA RTX 2060 6GB급 | 1080p High 60, 1440p High 30 |
| `REC-W11-AMD` | Windows 11 Home/Pro 25H2, 최신 누적 업데이트 | 권장 AMD | AMD RX 6600 8GB급 | 1080p High 60, 1440p High 30 |

Windows 10과 11, NVIDIA와 AMD의 기능 경계도 각 장비에서 새 게임,
재실행, 해상도/창 모드 전환, 자막·문서 UI, 저장·불러오기와 양 엔딩까지
확인한다. 한 OS나 한 GPU 공급사 결과가 빠지면 그 대안은 지원 목록에서
제거하거나 게이트를 `BLOCKED`로 유지한다.

Windows-v1의 Windows 11 인증 범위는 Home/Pro 25H2로 한정한다.
26H1을 포함한 다른 기능 업데이트는 실행될 수 있어도 출시 지원으로
광고하지 않는다. 새 Windows 11 기능 업데이트를 지원하려면 해당 버전의
NVIDIA·AMD 기능 검사와 최소/권장 성능 경로를 추가하고 문서 버전을 올린다.

## 해상도·화면 지원 매트릭스

| 출력 해상도 | 지원 상태 | 인증 프리셋과 목표 |
|---|---|---|
| 1280×720 | 지원 | 최소 사양, Low, 30fps. UI·자막·문서의 최소 가독성 기준 |
| 1920×1080 | 주 지원 | 최소 사양 Low 30fps와 권장 사양 High 60fps를 모두 인증 |
| 2560×1440 | 지원 | 권장 사양, High, 30fps. UI·자막·문서 배치도 별도 확인 |
| 3840×2160 | 미인증 | 실행 선택지가 노출되더라도 성능·가독성·메모리를 보증하지 않으며 출시 지원 해상도로 광고하지 않음 |

Windows-v1 인증 화면비는 16:9다. 창 모드, 테두리 없는 창 모드와 전체
화면에서 위 세 지원 해상도를 각각 확인한다. 초광폭, HDR과 다중 모니터는
출시 인증 범위 밖이며 필수 정보가 잘리는 명백한 결함만 P0/P1로 처리한다.
첫 실행 기본값은 1920×1080 High, 60fps, VSync, 동적 해상도 끔과
`r.ScreenPercentage=100`이다. 네이티브 화면 설정은 세 지원 해상도와 세
창 모드, Low/High, VSync, 30/60/무제한 프레임만 노출한다. 변경 적용 뒤
10초 유지 확인이 없으면 이전 해상도·화면 모드·품질·VSync·프레임 제한을
전부 복원해야 하며, 이 자동 복원도 각 GPU 계열 기능 검사에 포함한다.

성능 인증은 동적 해상도를 끄고 `r.ScreenPercentage=100`인 네이티브
렌더링으로 진행한다. TSR을 사용하더라도 내부 해상도를 낮춰 기준을
통과시키지 않는다. 출시 설정에 별도 TSR Quality 프리셋을 제공할 수
있지만 네이티브 인증 결과를 대체하지 않는다. VSync와 프레임 제한은
측정 중 끄고, 화면 찢김 확인은 VSync를 켠 별도 기능 검사에서 수행한다.

## 성능 합격선

프레임 시간 `p95`는 워밍업을 제외한 프레임의 95백분위 값이다.
`1% low`는 가장 느린 1% 프레임 시간의 산술 평균을 밀리초로 계산한 뒤
`1000 / 평균 프레임 시간`으로 환산한다. 평균 fps만으로 합격시키지 않는다.

| 인증 경로 | 프레임 시간 p95 | 1% low | 워밍업 뒤 단일 hitch | 게임 프로세스 메모리 | 전용 VRAM |
|---|---:|---:|---:|---:|---:|
| 최소 사양, 1280×720 Low 30 | 33.33ms 이하 | 25fps 이상 | 100ms 초과 0건 | peak committed 12.0GB 이하 | peak 5.5GB 이하 |
| 최소 사양, 1920×1080 Low 30 | 33.33ms 이하 | 25fps 이상 | 100ms 초과 0건 | peak committed 12.0GB 이하 | peak 5.5GB 이하 |
| 권장 사양, 1920×1080 High 60 | 16.67ms 이하 | 50fps 이상 | 50ms 초과 0건 | peak committed 12.0GB 이하 | peak 5.5GB 이하 |
| 권장 사양, 2560×1440 High 30 | 33.33ms 이하 | 25fps 이상 | 100ms 초과 0건 | peak committed 12.0GB 이하 | peak 5.5GB 이하 |

로딩 화면과 의도적으로 멈춘 엔딩 정지 컷은 프레임 통계에서 분리하되
멈춘 이유와 구간을 원본 trace에 주석으로 남긴다. 플레이 가능한 상태의
스트리밍, 체크포인트, 저장 완료, 문 열림과 음악 전환은 제외하지 않는다.
셰이더 컴파일이나 에셋 누락 때문에 생긴 hitch는 워밍업 사유로 숨길 수
없다.

설치된 Shipping 배포물의 총 파일 크기는 8.0GB 이하여야 한다. 설치 전
12GB 여유 공간 요구에는 압축 해제·prerequisite·업데이트 임시 공간을
포함한다. 사용자 세이브, 크래시 덤프와 로그는 설치 크기에 포함하지
않지만 정상 1회 완주 뒤 `Saved` 폴더가 1.0GB를 넘으면 실패다.

## 측정 절차

1. 검증할 40자리 Git SHA와 작업 트리 clean 상태를 고정하고 G3·G5를
   통과한 같은 SHA에서 G6 하네스가 만든 Win64 Shipping 패키지를 사용한다.
   Development/Editor 수치는 진단용이며 출시 승인을 대신하지 않는다.
2. Windows 업데이트, GPU 드라이버 버전, CPU/GPU/RAM, 저장 장치, 출력
   모드와 게임 설정을 결과 매니페스트에 기록한다. 전원 모드는 AC 전원과
   Windows `최고 성능`으로 고정하고 불필요한 오버레이·녹화 프로그램은
   끈다.
3. `필수 장비 축`의 여섯 장비에서 각자 지정된 프리셋을 실행한다.
   게임을 실행한 뒤 2분간 워밍업하고, 다음 장면을 포함한 동일 입력
   경로를 각 프리셋마다 세 번 실행한다. 재부팅 뒤 첫 실행도 별도로
   보존한다.
   - 프롤로그: 골목→편의점→공동현관→엘리베이터→403호
   - 밤1·밤2: 403호→4층 복도→1층 계량기함·관리실, 저널과 문서 UI
   - 밤3·밤4: 5층 공동벽→옥상 저수조→위층 사람이 쫓는 계단→엔딩 선택
4. 세 반복 중 어느 하나라도 표의 합격선을 넘으면 실패다. 가장 좋은
   반복만 골라 제출하지 않는다.
5. `stat unit`, `stat gpu`, Unreal Insights의 CPU/GPU/frame/loadtime/
   memory trace, `MemReport`, 실행 로그와 화면 설정 캡처를 원본 그대로
   보존한다. GPU 캡처가 지원되지 않는 장비는 도구명과 누락 사유를
   기록하되 프레임·메모리 측정은 생략하지 않는다.
6. 결과 폴더의 모든 파일에 SHA-256을 계산하고 커밋 SHA, 패키지
   매니페스트, 장비 정보, 각 반복의 p95·1% low·최대 hitch·RAM·VRAM·
   설치 크기를 `performance-result.json`에 기록한다.

첫 실행 셰이더 준비와 재실행의 차이는 별도 행으로 남긴다. 반복 측정
사이에 설정, 드라이버, 패키지 또는 맵이 바뀌면 같은 표본으로 합치지
않는다.

## 설정창 뒤의 렌더링 — 2026-09-30

화면 설정, 접근성, 소리·밝기와 키 설정이 화면을 덮을 때는 해당 플레이어의
`UGameViewportClient::bDisableWorldRendering`으로 3D 렌더링을 쉰다. Canvas로
그리는 글자와 미리 보기는 계속 갱신한다. 메뉴를 닫거나 컨트롤러가 끝나면 이전
렌더링 상태로 돌아간다. 게임 장면이 보이는 일시 정지 화면은 계속 그린다.

실제 화면 검사에서 설정을 열었을 때의 중단과 일시 정지·타이틀로 돌아왔을 때의
복원을 함께 확인한다. 메뉴 비용을 줄이는 변경이며 플레이 중의 GPU 성능과
여섯 장비의 출시 합격선을 대신하지 않는다. 동작은 설치된 UE 5.8의
`GameViewportClient.cpp`와 [Epic의 API 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UGameViewportClient)에서 확인했다.

## 2026-09-30 갱신 비용과 측정 보완

5층 바닥 흔적은 개수가 그대로여도 위치와 방향이 바뀌고, 최대 96개가
차면 가장 오래된 자국이 새 자국으로 교체된다. 이전에는 개수만 비교해
이 두 경우를 화면에 반영하지 못했다. 이제 변경 번호가 달라질 때만
배열을 읽고, 인스턴스 수가 같으면 기존 버퍼의 좌표를 갱신한다. 확인
주기도 매 프레임의 누적 계산에서 실제 0.25초 컴포넌트 Tick으로 옮겼다.
높이를 함께 검사해 다른 층의 발자국이 같은 평면 위치의 5층 바닥에
찍히던 문제도 고쳤다.

기존 게임플레이 프로브는 실제 ISM 위치·회전, 다른 층 제외, 96개 포화
이후의 교체, 초기화까지 확인한다. 런타임에서 이 검사를 통과하면
`MISSINGFLOOR_DUST PASS movement rotation floor_filter capacity reset`을
기록한다. 정적 검사 통과만으로 이 영수증을 대신하지 않는다.

오디오 버스가 대본 소리로 차 있을 때 새 걸음을 추적 없이 계속 재생하던
경로를 막았다. 대본은 그대로 두고 추가 걸음만 40ms에 걸쳐 걷는다.
일반적인 오래된 소리 교체의 120ms 꼬리와 상시 베드 보호는 유지한다.
이는 [Epic의 동시 발음 관리](https://dev.epicgames.com/documentation/unreal-engine/sound-concurrency-reference-guide)가
설명하는 최대 발음 수, 우선순위와 짧은 퇴출 페이드를 현재 버스 구조에
적용한 것이다.

High의 Lumen 반사는 기존 거칠기 한계 0.25를 유지한다. 다만 전역
`r.Lumen.Reflections.MaxRoughnessToTrace` 강제를 없애고
`ReflectionQuality@2`의 `MaxRoughnessToTraceClamp`로 옮겼다. 장면의
포스트 프로세스가 더 낮은 한계를 지정하면 이제 그 값도 적용된다.
[Epic의 Lumen 성능 안내](https://dev.epicgames.com/documentation/unreal-engine/lumen-performance-guide-for-unreal-engine?lang=en-US)와
설치된 UE 5.8 `LumenReflections.cpp`에서 이 차이를 확인했다. Low의
기존 확장성 경로와 High의 기본 화질을 바꾸는 최적화는 아니다.

`summarize_runtime_profile.py`는 출시 계약에 필요한 1% low와 50ms 초과
프레임 수도 기록한다. 가장 느린 표본 수는 전체의 1%를 올림하고 그
프레임 시간 평균의 역수를 쓴다. 손상된 행과 음수·0 프레임 시간은 실패로 처리한다.
2026-09-18의 기존 CSV를 다시 읽는 것은 요약 도구 검증에만 쓰며,
이번 코드의 성능 측정 결과로 사용하지 않는다.

2026-09-30에 검토한 [Tom Looman의 UE 5.8 정리](https://tomlooman.com/unreal-engine-5-8-performance-highlights/)와
[DX12 PSO 검증 경험](https://tomlooman.com/psocaching-unreal-engine/)도
실행 경로를 측정하고 첫 노출 때의 지연을 따로 확인하는 데 초점을 둔다.
현재 절차 생성 월드에 Nanite·HLOD·MegaLights를 일괄 적용하지 않고,
기존 LOD·ISM·PSO 준비를 유지한 채 실제 결함과 불필요한 갱신부터 줄인다.
GPU 시간 개선 여부는 같은 장면의 D3D12 재측정으로 판정한다.

## 2026-08-06 적용 구조

아래 표는 목표가 아니라 현재 소스와 자동 검증이 강제하는 구현 계약이다.
성능 수치 PASS와는 구분한다.

| 영역 | 현재 구현 | 회귀 합격 조건 |
|---|---|---|
| 정적 메시 LOD | `generate_meshes.py`가 근접 조사용 hero를 제외한 제작 메시를 `SmallProp`/`LargeProp` LOD 그룹으로 빌드한다. LOD 전환은 고정 거리 대신 화면 점유율을 따른다. | `validate_baked_art_assets.py`에서 모든 non-hero 메시가 LOD 2개 이상이어야 한다. |
| 사진측량 프롭 LOD | 수입된 50개 Static Mesh를 크기 용도에 따라 `SmallProp`/`LargeProp`으로 분류하고, 기존 LOD가 없을 때만 축소 LOD를 빌드·저장한다. 2026-08-06 적용 결과 50개를 갱신했다. | UAsset 감사에서 50개 모두 LOD 2개 이상이어야 한다. 5만 vertex 이상은 Nanite 검토 대상으로만 기록하며 자동 전환하지 않는다. |
| 편의점 반복 재고 | 과자·컵라면·삼각김밥·PET 음료·가격표 1,301개를 메시/머티리얼별 `UInstancedStaticMeshComponent` 23개 배치(상한 24개)로 묶는다. | 런타임에서 `instances=1301`, `batches<=24`, 무충돌, Static mobility와 16~22m start/end 컬링 값을 직접 검사한다. |
| 작은 프롭의 Lumen 비용 | 위 재고는 직접광·머티리얼·화면 추적은 유지하고 distance-field 장면 기여, 데칼 수신, 내비게이션과 그림자 중복을 줄인다. 몸체처럼 실루엣에 필요한 배치만 그림자를 남긴다. | `REBIRTH_RELEASE PASS store_instancing` 영수증이 없으면 A/B 런타임 검증이 실패한다. |
| 유휴 CPU | 손전등은 켜진 동안만, 스트레스 컴포넌트는 공포 값·심박 억제 상태가 실제로 진행되는 동안만 Tick한다. 상호작용 탐색은 기존 10~15Hz 타이머를 유지한다. | Component Tick은 기본 활성 상태로 시작할 수 없으며 정적 검증이 `bStartWithTickEnabled=true`를 차단한다. |
| 설정 HUD | 화면·접근성 설정은 공용 `IGSettingsMenuLayout`에서 해상도별 좌표와 포인터 판정을 계산한다. 색·표면 토큰과 하나의 임시 라운드 마스크를 재사용하며, 메뉴가 닫힌 프레임에는 설정 셋·글리프를 그리지 않는다. | 프런트엔드 계약이 렌더·히트 테스트의 공용 좌표 사용을 검사하고, Shipping 프로브가 720p~1440p 설정 PNG 8장과 키보드·게임패드 경로를 통과해야 한다. 프레임 비용 PASS는 별도 Shipping trace로 판정한다. |
| 하단 대화·자막 HUD | 별도 Widget 트리와 raw binding을 늘리지 않고 기존 네이티브 HUD 한 경로에서 이벤트로 큐만 갱신한다. 대화 6개·환경음 4개로 메모리를 제한하고, 한글 줄바꿈 결과는 페이지·크기·폭이 달라질 때만 다시 계산한다. 숨김 상태에서는 패널·글리프를 그리지 않는다. | 정적 대화 계약, 720p~1440p 경계 오라클과 실제 D3D12 프런트엔드 프로브가 안전 영역·최대 3줄·두 레인·200% 배율을 통과해야 한다. 성능 PASS는 별도 Shipping trace로 판정한다. |
| 텍스처·PSO | 텍스처 스트리밍과 component/global-shader PSO precache를 Shipping 설정에 명시한다. VRAM 풀 크기는 장비 실측 전에는 고정하지 않는다. | `DefaultEngine.ini`의 네 설정과 `stat Streaming`, `stat PSOPrecache`, Insights 증거를 함께 확인한다. |
| 품질 확장성 | Volumetric Fog의 프로젝트 우선순위 고정을 제거해 Low에서는 엔진 scalability가 비활성화하고 High에서는 낮은 해상도 볼륨으로 유지한다. | `DefaultEngine.ini`에 루트 `r.VolumetricFog` 값이 다시 들어오면 정적 검증이 실패한다. |

편의점의 16~22m 값은 **LOD 전환값이 아니라 개별 소형 재고의
per-instance start/end cull 범위**다. 재질이 `PerInstanceFadeAmount`를
소비할 때만 이 구간이 시각적 fade가 되며, 그렇지 않은 불투명 재질은
22m에서 GPU cull된다. 매장 외벽, 조명, 상호작용 물병과 모든 서사
증거물은 이 배치에 넣지 않는다. 따라서 멀리서 매장 실루엣과 빛은 남고,
읽을 수 없는 포장지만 사라진다. fade 재질을 도입하면 PSO·overdraw와
팝 감소를 같은 Shipping 장면에서 A/B 측정한 뒤 채택한다.

현재 월드는 `BeginPlay`에서 절차적으로 조립되므로 에디터가 미리 굽는
HLOD 클러스터를 적용하지 않는다. 향후 환경을 저장된 Level/World
Partition 셀로 이전하면 그때 HLOD를 빌드하고 셀 경계 이동 hitch와
occlusion 결과를 다시 측정한다. 지금 HLOD를 켜는 것은 실제 생성 프롭을
포함하지 못해 복잡도만 늘린다.

Nanite도 기능 이름만 보고 일괄 적용하지 않는다. 현재 생성 프롭은 저폴리
메시이며 화면 크기 기반 LOD가 더 예측 가능하다. 사진측량 메시처럼 실제
삼각형·머티리얼 비용이 큰 에셋은 `Nanite`와 전통 LOD 두 후보를 동일
Shipping 경로에서 비교하고 GPU 시간, fallback mesh, 메모리와 그림자
결과가 나아질 때만 전환한다.

정식 프로파일은 추측 대신 Unreal Insights와 `stat unit`, `stat gpu`,
`stat RHI`, `stat InitViews`, `stat Streaming`, `stat PSOPrecache`를 함께
쓴다. 특히 첫 편의점 진입 전후의 draw call/primitive 수, 위층 사람이
처음 달려드는 순간의 PSO `Too Late`/miss, 밤마다의 texture-pool over budget
여부를 별도 북마크로 남긴다.

관련 UE 5.8 기준은 [성능 프로파일링](https://dev.epicgames.com/documentation/en-us/unreal-engine/introduction-to-performance-profiling-and-configuration-in-unreal-engine),
[Static Mesh LOD](https://dev.epicgames.com/documentation/en-us/unreal-engine/creating-and-using-lods-in-unreal-engine),
[Instanced Static Mesh](https://dev.epicgames.com/documentation/en-us/unreal-engine/instanced-static-mesh-component-in-unreal-engine),
[가시성·오클루전 컬링](https://dev.epicgames.com/documentation/en-us/unreal-engine/visibility-and-occlusion-culling-in-unreal-engine),
[텍스처 스트리밍](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-configuration-in-unreal-engine),
[PSO precaching](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine),
[Actor Tick](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-ticking-in-unreal-engine),
[Scalability](https://dev.epicgames.com/documentation/unreal-engine/scalability-in-unreal-engine?lang=en-US),
[Memory Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/memory-insights-in-unreal-engine),
[DPI Scaling](https://dev.epicgames.com/documentation/en-us/unreal-engine/dpi-scaling-in-unreal-engine),
[Safe Zones](https://dev.epicgames.com/documentation/unreal-engine/umg-safe-zones-in-unreal-engine?lang=en-US),
[UI 최적화](https://dev.epicgames.com/documentation/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine)을 따른다.

## 2026-08-06 로컬 D3D12 스모크

이 결과는 현재 개발 PC의 `UnrealEditor-Cmd -game`, 1920×1080 High,
D3D12, offscreen 실행이다. Shipping 패키지나 최소·권장 장비 결과가 아니며
G6 판정을 바꾸지 않는다. CSV profiler가 수집한 2,514프레임 중 맵 로드와
초기 준비 700프레임을 제외한 1,814프레임을 같은 산식으로 계산했다.

| 항목 | 측정값 |
|---|---:|
| 프레임 시간 p95 / p99 / 최대 | 24.43ms / 29.23ms / 34.88ms |
| 1% low | 32.54fps |
| 50ms 초과 / 100ms 초과 hitch | 0 / 0 |
| Game Thread p95 | 4.98ms |
| Render Thread critical path p95 | 23.75ms |
| GPU p95 / 최대 | 13.86ms / 14.98ms |
| RHI draw calls 평균 / p95 | 145.08 / 155 |
| primitives 평균 | 13,953.31 |
| 프로세스 working set 최대 | 3,687MB |
| 전용 GPU 메모리 최대 | 2,735.80MB |
| texture wanted mip 충족 평균 | 99.98% |
| Graphics PSO hitch | 0건 |

GPU에는 60fps 예산 이내의 여유가 있었지만 전체 프레임 p95는 16.67ms를
넘었다. Editor·CSV 계측 오버헤드와 Render Thread 비용이 섞였어도 합격선은
완화하지 않는다. 이 결과는 1080p High 30fps 스모크만 통과한 것으로
기록하고, 60fps는 같은 SHA의 Shipping 패키지를 권장 장비에서 세 번
측정할 때까지 미승인으로 둔다.

원본은 `Saved/Validation/PerformanceOptimization/runtime_d3d12_smoke/`의
CSV, `csv-stats.json`, `csv-critical-stats.json`, `render-stats.json`,
`RebirthRelease_EndingA_D3D12.utrace`와 오류 0건의
`RebirthRelease_EndingA_D3D12_trace.log`다. trace SHA-256은
`E0177A74C3281C1AA52602F1F12AFC16E616637373306EB7207AC47B250BEA21`이다.

## 런타임 예산 규칙

- Actor Tick은 기본 비활성화하고 이벤트 또는 제한 주기 타이머를 사용한다.
- 플레이 중 동기 에셋 로드는 금지하며 다음 밤에 쓸 에셋의 soft reference를
  미리 비동기 로드한다.
- 상호작용 trace는 플레이어당 하나, 10~15Hz를 기본값으로 한다.
- 공간 오디오는 virtualization과 streaming을 사용하고 동시 활성 음성을
  관리한다.
- 일반 소품 텍스처는 1K, hero 소품은 2K를 출발점으로 하며 4K는 화면
  점유와 메모리 근거가 있을 때만 사용한다.
- 반복되는 비상호작용 정적 메시를 개별 컴포넌트로 생성하지 않는다.
  메시·머티리얼·그림자 정책이 같은 항목은 ISM으로 묶고, 작은 프롭에는
  거리 fade/cull을 지정한다.
- 일반 Static Mesh LOD는 월드 거리 숫자가 아니라 화면 점유율로 고른다.
  서사 증거물은 근접 판독 LOD를 보존하고 자동 축소 결과를 눈으로 확인한다.
- Lumen을 사용할 때 shadow-casting movable light의 수와 영향 반경을
  장면별로 제한한다.
- 정적 고밀도 환경 메시에는 Nanite를 비교 검토하되 저폴리 프롭에
  일괄 적용하지 않고, 플레이 캡슐용 단순 collision을 별도로 제공한다.

## 판정

여섯 필수 장비의 지정 인증 경로, OS·GPU 공급사별 기능 검사, 설치
크기와 `Saved` 증가량이 모두 같은 SHA에서 PASS여야 Windows-v1 성능
G6 성능 항목을 통과한다. 한 장비·OS·GPU 공급사·해상도가 누락되면
`PARTIAL`이 아니라 `BLOCKED`, 수치가 초과되면 `FAIL`이다.

2026-08-06 당시 커밋하지 않은 변경이 포함된 검증용 Shipping 패키지는
`Saved/StagedBuilds/RebirthShipping/20260806T072107035Z_51232/`에 있었다.
당시의 88파일·919,396,939바이트와 자동 검사 기록은 현재 후보의 승인 근거로
쓰지 않는다. 현재 후보도 최소·권장 여섯 장비의 원본 trace와
`performance-result.json`이 갖춰지기 전까지 판정은 **BLOCKED**다.

## 2026-09-30 화면 설정과 성능 측정 절차

네이티브 화면의 기본 안티앨리어싱을 TAA로 바꿨다. 같은 High 설정으로 비교한
에디터 진단에서 TSR보다 후처리 비용이 줄었고, 벽·계단·잔류 흔적·옥상 11지점은
기존 밝기 범위를 유지했다. 화면을 낮은 해상도로 그려 이익을 계산하지 않는다.
TSR은 낮은 내부 해상도를 복원할 때 강점이 있지만 출력 해상도에서 처리하는
히스토리 비용도 있다. [Epic의 TSR 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine)을
참고해 현재 네이티브 화면에 맞는 방식으로 비교했다.

이 비교는 안전한 저장 읽기를 적용하기 전 에디터 진단이다. 새 Shipping
후보의 성능 결과는 아래에 별도로 기록한다. 품질 단계별 내부 해상도는 모두
100%이며, 예전 설정에 71%·87%가 남아 있으면 시작할 때 보정한다. 기존 품질
단계·VSync·프레임 제한은 유지한다. 메뉴에서 표시하는 품질과 실제 적용값도
배포본의 설정 순회 검사에서 확인한다.

골목 가로등 세 개는 아래로 향한 실제 등기구에 맞춰 스포트라이트로 바꿨다.
밝기·거리·광원 크기는 유지하고 내부각 65도, 외부각 75도로 바닥을 비춘다.
Point와 Spot의 그림자 차이는 [Epic VSM 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine)에서
확인했다. 태양과 달, 실내 그림자와 실시간 하늘 캡처는 유지했다.

`Run-WindowsRuntimeProfile.ps1`은 같은 Shipping 파일을 독립 사용자 폴더에서
세 번 실행한다. 실제 120초 워밍업 동안 밤 시계와 AI만 멈추고 장면은 그린다.
이후 기본 밤 검수 경로의 0~16단계와 포획·복원 18단계를 전부 기록한다.
캡처 전용 FXAA 전환과 PNG 저장은 이 성능 경로에서 사용하지 않는다.

프레임 시간은 엔진 프레임 번호와 함께 원본에 남기고, GPU 결과는 엔진의
히스토리 큐에서 한 번씩 꺼내 별도 파일에 기록한다. 프레임 누락, 잘못된 순서,
설정 변경, GPU 결과 손실을 검사한다. 내부·보조 화면 배율 100%, 동적 해상도
끔, 프레임 제한·VSync 끔도 실제 관측값으로 확인한다. 메모리는 초당 한 번의
표본이고 GPU 장면 번호는 결과를 받은 시점이다. 종료 직전 GPU 결과는 기다리지
않으므로 이 원본은 완전한 GPU trace를 대신하지 않는다.

아래 0.2.3 측정은 오프스크린으로 돌아가 화면 출력 비용을 제외한다. 현재 장비의
자동 장면 진단이며, 위의 여섯 장비와 실제 화면·전체 보행 경로에 대한 인증은
별도로 진행한다. 결과 JSON의 `shippingReleaseCertified`는 항상 false다.

타이틀 수정 전 Safe 후보(`98987a3`)를 Ryzen 9 7900X·RTX 3060에서 측정한
두 실행은 다음과 같다. 1080p High, TAA, 내부·보조 해상도 100%, VSync와
프레임 제한 끔을 실제 기록으로 확인했다. 각 실행은 120초 워밍업 뒤 약 64초의
자동 경로를 기록했다.

| 실행 | 프레임 p95 | 1% low | GPU p95 | 해당 경로 기준 |
|---|---:|---:|---:|---|
| 1 | 15.200ms | 59.328fps | 14.725ms | 통과 |
| 2 | 17.085ms | 34.449fps | 15.214ms | 실패 |

두 번째 실행에서는 느린 프레임이 특정 구간에 몰렸다. GPU 시간도 같은 시간대에
늘었으며, 이 프레임을 이상치로 빼거나 기준을 낮추지 않았다. 세 번째 실행은 완료
기록과 원본이 없어 세 번의 반복 검사를 끝낸 것으로 세지 않는다. 중단 원인은
확인하지 못했다. 원본 두 벌은 `Saved/Validation/ShippingProfile023Safe-HighTAA/`
아래에 보관한다. 이 결과를 수정 후 배포본의 성능으로 표시하지 않는다.

Shipping에서 GPU 메모리 조회값은 사용할 수 없었다. 프로세스 메모리의 초당
표본은 남아 있지만 GPU 메모리 예산 통과 여부는 이 자료로 판단할 수 없다.

## 2026-10-01 타이틀 수정 후 Shipping 3회 측정 (0.2.3, 오프스크린)

커밋 `aec8fd3ea7ad89e3bec64e245c62e5c60520061f`에서 만든 Shipping 후보를
독립 사용자 폴더 세 곳에서 실행했고, 세 번 모두 이 자동 경로의 프레임 합격선을
통과했다. 실행 파일 SHA-256은
`20d2c75475f63276e99127070671d488c4f8d0c1cc323d5ead0d855e25f032ea`다.
상위 요약은 작업 트리가 깨끗했고 측정 전후 패키지가 같았음을 기록한다.
앞선 Safe 후보의 두 실행과 합치지 않으며, 당시 Run2 실패 판정도 유지한다.

측정 장비는 Ryzen 9 7900X·RTX 3060 한 대이며 GPU 드라이버는 595.79다.
D3D12 오프스크린, 1920×1080 High(`QualityLevel=2`), TAA(`AntiAliasingMethod=2`),
내부·보조 화면 배율 100%, 동적 해상도·VSync·프레임 제한 끔을 세 실행의
관측값으로 확인했다. 매번 실제 120초 워밍업 뒤 같은 0~16·18단계 경로를
63.820~63.824초 기록했다. 경로 중 순간이동과 연출 전환도 집계에 포함한다.

| 실행 | 집계 프레임 | 프레임 p95 | 1% low | GPU p95 | 최대 프레임 시간 | 50ms 초과 | 해당 경로 기준 |
|---|---:|---:|---:|---:|---:|---:|---|
| 1 | 4,877 | 16.267ms | 55.847fps | 15.783ms | 22.172ms | 0건 | 통과 |
| 2 | 4,838 | 16.571ms | 55.122fps | 16.039ms | 22.802ms | 0건 | 통과 |
| 3 | 4,828 | 16.448ms | 54.340fps | 15.893ms | 23.957ms | 0건 | 통과 |

세 실행 모두 프레임 p95 16.67ms 이하, 1% low 50fps 이상, 50ms 초과 0건을
만족한다. GPU 큐가 보고한 누락(`Disjoint`)은 0건이며 GPU 시간은 별도 큐 원본에서
계산했다. GPU의 장면·시각은 수신 기준이고 종료 때 미도착 결과를 기다리지
않는다는 앞 절의 범위는 그대로다.

프로세스 committed 메모리의 초당 표본 최댓값은 세 실행 중 4,614.469MiB였다.
순간 최대 메모리를 측정한 값은 아니다. VRAM은 세 요약 모두 `null`로,
전용 메모리 예산은 아직 검증하지 못했다. 실제 화면에 표시하는 Present 비용도
이 오프스크린 검사에 포함되지 않는다.

원본과 각 실행의 `summary.json`, 설정 전후 기록은
`D:/MissingFloor-ReleaseValidation-20261001/HighTaaProfile/Run1`~`Run3`에 있다.
상위 `HighTaaProfile/summary.json`과 `Release023Title-validation.json`의
`shippingProfile`에도 같은 값과 원본 해시를 남겼다.

현재 추가 검증 장비와 외부 테스터는 확보되지 않았다. 필수 여섯 장비의 성능·기능
검사와 사람이 직접 하는 전체 경로·양 엔딩 완주 검수는 남아 있다. 이번 결과는
한 PC의 자동 경로 3회 통과이며, 정식 출시 인증 `shippingReleaseCertified`는
계속 `false`, Windows-v1 전체 성능 판정은 **BLOCKED**다.

## 2026-10-01 실제 창에서 측정한 0.2.4

빌드 커밋은 `eaa202c7b380149cee0642c0a8266e9236441814`이며, Shipping 실행 파일 SHA-256은
`ae35ea54ab117b720319e94041c7fd0e21aa2717ec0f81aaeadb45a1ec63b00e`다.
창 제목에 남은 `IndieGame`을 `Missing Floor`로 고쳤다. 아래 측정 전후에 같은 89개 파일의
해시를 대조했다. 성능 측정 코드의 커밋과 네 스크립트의 해시도 실행마다 남겼다.

장비는 Ryzen 9 7900X·RTX 3060·메모리 32GB, Windows 11 빌드 26200, GPU 드라이버
595.79다. D3D12 창 모드, 네이티브 화면 배율 100%, TAA를 사용하고 VSync·프레임 제한은
껐다. 다른 프로그램을 종료하거나 드라이버 캐시를 지우지는 않았다. 현재 데스크톱의
자동 야간 진단이며, 지정된 최소·권장 사양이나 전체 보행 경로를 대신하지 않는다.

| 조건 | 프레임 p95 (ms) | 1% low (fps) | GPU p95 (ms) | 최대 프레임 (ms) | 전용 VRAM 표본 최대 (GB) | 해당 경로 프레임 기준 |
|---|---:|---:|---:|---:|---:|---|
| 1080p High · 준비 0초 | 16.410 | 29.964 | 15.701 | 48.998 | 3.074 | 실패 |
| 1080p High · 준비 0초 재실행 | 15.148 | 56.315 | 14.461 | 43.074 | 3.109 | 통과 |
| 1080p High · 준비 120초 · 1회 | 16.541 | 56.234 | 15.969 | 19.508 | 3.075 | 통과 |
| 1080p High · 준비 120초 · 2회 | 16.630 | 54.154 | 16.009 | 28.145 | 3.110 | 통과 |
| 1080p High · 준비 120초 · 3회 | 17.438 | 38.894 | 16.781 | 29.991 | 3.073 | 실패 |
| 720p Low · 준비 120초 재측정 | 7.000 | 117.420 | 6.405 | 15.142 | 2.316 | 통과 |
| 1080p Low · 준비 120초 | 12.460 | 74.319 | 11.983 | 15.223 | 2.574 | 통과 |
| 1440p High · 준비 120초 | 27.156 | 34.445 | 26.572 | 32.036 | 3.437 | 통과 |

1080p High의 120초 준비 후 반복 검사에서는 세 번째 실행이 실패했다. 프레임 p95는
16.67ms를 넘고, 1% low는 50fps보다 낮았다. 네 번째 밤 옥상 전원·밸브 장면(13단계)의
GPU p95도 24.950ms까지 늘었다. 통과한 두 실행만 골라 60fps를 보증하지 않는다.
720p Low·1080p Low·1440p High는 각각 한 번의 추가 진단이므로 세 번 반복한 인증으로
세지 않는다. 표의 실패 판정과 원본은 그대로 보존했다.

준비 0초 검사는 새 사용자 폴더에서 야간 진단 장면으로 바로 들어간다. 나머지 화질
검사가 끝난 뒤 같은 조건으로 한 번 더 실행했다. 일반 플레이에서 새 게임을 고르는
동선과 다르며, 재부팅 후 첫 실행이나 빈 드라이버 캐시를 검사한 결과도 아니다.
첫 표본에서 포획·복귀 장면(18단계)의 프레임 p95는 32.927ms였다. 지연의 원인이
PSO 컴파일이라고 확정할 자료는 없어, 준비 시간을 늘리는 것을 게임 최적화로 표시하지 않는다.

### 실제 화면 출력과 GPU 메모리

[PresentMon 2.6.0](https://github.com/GameTechDev/PresentMon/releases/tag/v2.6.0)은
2026년 9월 21일 공개된 버전을 사용했다. 공식 릴리스의 SHA-256과 받은 실행 파일을
대조하고 `--v1_metrics`로 출력했다. 모든 실행에서 게임 PID와 `Missing Floor` 창 제목,
실제 표시된 프레임을 확인했다. 표시된 프레임은 실행별 2,448~10,866개였다.

PresentMon의 전체 구간에는 시작·준비 구간·종료가 들어 있다. 화면 표시 간격과
Present API 시간은 엔진의 약 64초 야간 경로 통계와 구분해 JSON에 남겼으며, 입력
장치에서 화면까지의 지연은 측정하지 않았다. VSync와 프레임 제한을 껐으므로 렌더한
모든 프레임이 약 60Hz 화면에 표시되지는 않는다. 버려진 Present 수를 곧바로 게임의
멈춤 횟수로 해석하지 않는다. 앞선 PresentMon 1.8.0 진단의 캡처 길이를 벗어난 표시
간격은 유효하지 않다고 기록했다.

최초 720p Low 측정에서는 외부 CSV의 시간 순서가 네 곳에서 뒤집혀 분석이 중단됐다.
게임 자체의 프레임 p95는 8.201ms, 1% low는 81.322fps였지만, 이 통과 결과로 외부
계측 오류를 덮지 않았다. 원본 행과 오류 기록을 보관하고, 시각이 뒤집힌 실행은
화면 표시 수·메모리와 유효한 시간 통계를 나누도록 분석기를 고쳤다. 표시 간격과
표시 대기 시간은 `null`로 두고 원래 보고된 통계는 별도 필드에 남긴다. 중복 프레임,
다른 PID, 해시 변조는 계속 거부한다. 수정 뒤 별도 사용자 폴더에서 다시 측정했다.
2026년 5월의 [PresentMon 개발자 제보](https://github.com/GameTechDev/PresentMon/issues/627)에서도
순서 역전 사례를 확인했으나, 이번 원인과 같다고 확정하지 않는다.
표에 든 여덟 실행의 외부 시간 통계에는 같은 순서 역전이 없었다.

Shipping의 엔진 내 VRAM 조회가 `null`인 경우를 0으로 바꾸지 않았다. Windows의
`GPU Process Memory` 카운터에서 해당 PID의 전용·공유 메모리를 약 1.27초 간격으로
읽었다. 전용 메모리 표본 최댓값은 여덟 실행 중 3.437GB였다. 전부 5.5GB 이하였지만
표본 사이의 순간 최대값은 보증하지 않는다. 조회할 수 없는 시작·종료 표본도 따로 센다.
프로세스 사이 공유분이 포함되는 Windows 집계 방식은
[Microsoft의 GPU 메모리 설명](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/)을 따른다.

프레임과 화면 출력은
[PresentMon 공식 측정 항목](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/README-ConsoleApplication.md)을,
첫 실행과 재실행을 나누는 판단은
[Epic의 PSO 사전 준비 안내](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine)를
참고했다. 도구와 문서의 공개 시점은 2026년 9월 30일을 기준으로 검토했다.

요약과 원본 해시는 [0.2.4 검수 기록](Validation/Windows-20261001-0.2.4.json)에 있다.
프레임 실패 해결, 재부팅 후 첫 실행, 전체 보행, 지정된 여섯 실제 PC와 사람의 완주·청취
검수가 남아 있으므로 Windows-v1 정식 출시 승인은 계속 보류한다.

## 2026-10-01 0.9.0 후보 — 벽 너머 그림자, 빠진 HUD 텍스처, PSO 캐시

### 무엇을 바꿨나

0.2.4 1080p High의 실패 구간을 개발 빌드의 `csvprofile`로 다시 나눠 보니 GPU 시간의
절반 가까이가 직접광이었고, 그중 대부분이 가상 그림자 맵 투영이었다. 벽이나 바닥판
너머에 있는 등의 감쇠 구가 이쪽 화면까지 걸쳐, 어차피 가려질 그림자를 픽셀마다 다시
계산하고 있었다. 조명 구역을 나눠 이런 등을 끈다.

- 별관 등은 옥상에 있을 때만 켠다. 4층에서 보이는 윗계단 등은 따로 둔다.
- 403호 문이 완전히 닫혀 있으면 방 안에서는 복도·계단·승강기 등을, 복도에서는 방
  등을 끈다. 문이 움직이기 시작하면 양쪽을 바로 다시 켠다.
- 층마다 있는 승강기 칸의 등을 그 층의 조명 구역에 넣었다.
- 지평선 아래에 있는 밤의 해는 그림자를 끈다. 대기 투과율이 이미 빛을 0으로 만들어
  V5 11지점 값은 그대로다.

같은 밤 경로를 에디터 개발 빌드로 잰 GPU 평균은 403호 16.7→11.5ms, 복도
19.9→10.5ms, 옥상 14.3→9.1ms였다.

코드가 경로로 불러오는 HUD 텍스처 여덟 장(두드리는 손 네 장, 기록 화면 종이, 대사
필름, 계량기함, 소리 맞춤 벽)이 0.2.4까지 배포 컨테이너에 없었다. 이 텍스처를 붙드는
Asset Manager 규칙이 `DefaultEngine.ini`에 있었는데, `UAssetManagerSettings`는
`config=Game`이라 언리얼이 그 줄을 읽지 않았다. 규칙을 `DefaultGame.ini`로 옮겼다.
Texture2D는 PrimaryAssetId를 스스로 내지 않으므로 `bShouldManagerDetermineTypeAndName`도
같은 자리에 둔다. 이 값이 없으면 쿠크가 ID 불일치 오류로 멈춘다. 이제 `Package-Windows.ps1`이
IoStore 목록을 뽑아 `check_package_contents.py`로 소스의 `/Game` 경로와 규칙 에셋이 모두
들었는지 보고, 하나라도 없으면 패키징을 실패로 끝낸다. 정적 검사
(`check_cook_references.py`)도 `DefaultEngine.ini`에 남은 규칙을 실패로 본다.

### 묶음 PSO 캐시

PSO 사전 준비는 부품이 쓸 PSO만 미리 컴파일한다. 후처리·UI·전역 셰이더처럼 그 밖에서
처음 그리는 PSO는 첫 실행에서 그리기를 멈추고 컴파일한다. `Scripts/Record-PsoCache.ps1`이
Development 배포본으로 타이틀, 입주 낮, README 장면 경로, 밤 경로(High·Low), 결말,
다섯째 밤을 돌며 PSO를 기록하고, 쿠크가 남긴 셰이더 안정 키와 합쳐
`Build/Windows/PipelineCaches/PSO_IndieGame_PCD3D_SM6.spc`를 만든다. 일곱 장면에서 262개를
모았고, 배포본 pak의 `IndieGame/Content/PipelineCaches/Windows/IndieGame_PCD3D_SM6.stable.upipelinecache`
(114,057바이트)로 들어간다.

설치된 UE 5.8에서는 이 형식에 문제가 둘 있었다.

- 쿠크는 D3D11용 SM5 안정 키도 남긴다. 함께 넣어 합치면 캐시 머리말의 형식(SM5)과
  실제 키(SM6)가 달라져 쿠크가 `PipelineCacheUtilities.cpp`의 검사에서 멈춘다. SM6 키만 쓴다.
- 셰이더 해시가 8바이트(xxhash64)로 줄었는데, `.spc`를 읽는 프록시는 8바이트짜리
  읽기를 모두 해시로 여긴다. 기본 사용 마스크(`r.ShaderPipelineCache.PreCompileMask=-1`)는
  9바이트 가변 정수로 저장되고, 읽을 때 뒤 8바이트가 해시로 읽혀 그다음부터 전부
  어긋난다. 같은 엔진이 쓴 파일을 기록 하나만으로도 다시 읽지 못했다. 기록할 때만 마스크를
  1로 둔다. 게임은 `r.ShaderPipelineCache.GameFileMaskEnabled`가 꺼져 있어 마스크와
  상관없이 모두 미리 컴파일한다.

기록 스크립트는 만든 캐시를 쿠크와 같은 조건(모든 형식의 키)으로 한 번 읽어 보고,
못 읽으면 실패로 끝난다. 기록은 2초마다 저장해 스스로 강제 종료하는 검사 장면도
PSO를 남긴다. 게임 설정의 낮음은 엔진 품질 단계 1이라 낮음 장면도 1로 기록한다.
테스트 도구의 성능 측정도 같은 이유로 낮음을 1로 고쳤다.

### 이 PC의 Shipping 측정

빌드 커밋은 `7cb2422d0c50f8d42d932f5ebac01c30b5dde607`, 게임 파일 89개다. 장비는
Ryzen 9 7900X·RTX 3060·메모리 32GB, Windows 11 빌드 26200, GPU 드라이버 595.79다.
0.2.4와 같은 방법으로 D3D12 창 모드, 네이티브 화면 배율 100%, TAA를 쓰고 VSync·프레임
제한은 껐다. PresentMon 2.6.0으로 화면 출력을, Windows 카운터로 프로세스의 전용 GPU
메모리를 기록했다. 준비 시간은 120초다(첫 줄만 0초).

| 조건 | 실행 | 프레임 p95 (ms) | 1% low (fps) | GPU p95 (ms) | 최대 프레임 (ms) | 50ms 초과 | 전용 VRAM 표본 최대 (GB) | 해당 경로 프레임 기준 |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| 1080p High · 준비 0초 | 1 | 12.190 | 65.68 | 11.363 | 47.40 | 0 | 3.101 | 통과 |
| 1080p High · 다시 잰 묶음 | 1 | 12.079 | 70.76 | 11.396 | 19.57 | 0 | 3.101 | 통과 |
| 1080p High · 다시 잰 묶음 | 2 | 12.102 | 70.14 | 11.290 | 21.39 | 0 | 3.102 | 통과 |
| 1080p High · 다시 잰 묶음 | 3 | 12.180 | 63.08 | 11.486 | 37.10 | 0 | 3.101 | 통과 |
| 1080p High · 첫 묶음 | 1 | 12.911 | 37.24 | 12.032 | 63.67 | 1 | 3.099 | 실패 |
| 1080p High · 첫 묶음 | 2 | 12.407 | 69.96 | 11.674 | 21.82 | 0 | 3.100 | 통과 |
| 1080p High · 첫 묶음 | 3 | 12.748 | 41.77 | 11.884 | 38.31 | 0 | 3.102 | 실패 |
| 1080p Low | 1 | 9.644 | 75.97 | 8.987 | 26.45 | 0 | 2.601 | 통과 |
| 1080p Low | 2 | 9.760 | 80.92 | 8.942 | 18.26 | 0 | 2.598 | 통과 |
| 1080p Low | 3 | 15.256 | 44.17 | 13.548 | 36.92 | 0 | 2.605 | 통과 |
| 720p Low | 1 | 6.729 | 118.09 | 5.278 | 16.76 | 0 | 2.340 | 통과 |
| 720p Low | 2 | 6.286 | 94.39 | 5.092 | 72.61 | 1 | 2.340 | 통과 |
| 720p Low | 3 | 5.867 | 142.24 | 5.113 | 14.98 | 0 | 2.346 | 통과 |
| 1440p High | 1 | 19.049 | 33.37 | 18.186 | 45.57 | 0 | 3.404 | 통과 |
| 1440p High | 2 | 20.380 | 41.13 | 19.387 | 30.85 | 0 | 3.407 | 통과 |
| 1440p High | 3 | 19.113 | 46.39 | 18.200 | 24.43 | 0 | 3.467 | 통과 |

0.2.4의 같은 조건과 견주면 1080p High 프레임 p95는 16.5~17.4ms에서 12.1ms 안팎으로,
1440p High는 27.2ms에서 19~20ms로, 1080p Low는 12.5ms에서 9.6~9.8ms로 줄었다.
Low 계열의 기준은 100ms 초과 0건이라 720p Low 2회의 72.6ms 한 프레임은 실패로 세지 않는다.

1080p High 첫 묶음은 세 번 중 두 번이 9·10단계에서 3초 남짓 GPU가 두 배로 느려져
1% low 50fps에 못 미쳤다. 1080p Low 3회의 15단계, 1440p High 1회의 14단계에도 같은
모양이 짧게 있었다. 튄 구간에서 GPU 클럭은 1905MHz(P0)에 제한 사유도 없었고,
`nvidia-smi pmon`으로 보니 같은 시각 이 PC에서 다른 Unreal 에디터·브라우저·Claude 앱이
GPU를 나눠 썼다. 개발 빌드에서 튄 프레임을 ProfileGPU로 잡은 기록도 그리기 수는 같고
TAA처럼 화면과 상관없는 패스까지 고르게 느려져 있었다. 이 PC는 다른 작업에도 쓰이고
있어서, 측정 실행기는 이제 어떤 Unreal·Blender 프로세스든 떠 있으면 기다렸다가 시작하고,
끝난 뒤 그사이 GPU를 쓴 프로세스를 함께 기록한다. 그렇게 다시 잰 1080p High 묶음은
세 번 모두 통과했다. 실패한 첫 묶음도 원본과 함께 남긴다.

준비 0초 실행은 새 사용자 폴더에서 야간 진단 장면으로 바로 들어간다. 0.2.4에서는 1% low
29.96fps로 실패했고, 이번에는 통과했다. 직전에 묶은 배포본(`44e235b`)의 같은 검사에서는
첫 장면에 들어가는 첫 프레임 하나가 55ms였다. 재부팅 뒤 빈 드라이버 셰이더 캐시에서의
첫 실행은 아직 재지 않았다.

개발 빌드 진단은 `-IGProfileGPUStages`에 `-IGProfileGPUSpikeMs`를 함께 주면 장면에
들어선 지 0.5초 뒤 GPU가 그 값을 처음 넘은 프레임도 기록한다. 이때 ProfileGPU 결과
창은 띄우지 않는다. 그 창의 Slate 그리기 2천여 회가 다음 프레임부터 GPU 시간에 섞였다.

요약과 원본 해시는 [0.9.0 검수 기록](Validation/Windows-20261001-0.9.0.json)에 있다.

### 다른 PC와 처음 하는 사람

배포 ZIP의 「테스트 도구」로 다른 PC에서도 같은 밤 경로를 세 번 돌려 결과를 ZIP으로
받는다. 받은 결과는 `Scripts/summarize_performance_reports.py`가 위 합격선과 견준다.
방법은 [테스트 안내](TESTING.md)에 있다. 이 PC 한 대의 결과는 Windows-v1의 여섯 장비
인증을 대신하지 않는다. Windows-v1 전체 성능 판정은 계속 **BLOCKED**다.

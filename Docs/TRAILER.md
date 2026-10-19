# 예고편 다시 만들기

지금 예고편은 62초, 1920×1080, 30fps다. 한국어판과 영어판은 같은 촬영분에 글자만 다르다.
2026-10-08에 다시 찍고 다시 엮었다.

## 원칙

- **카메라를 흔들지 않는다.** 예전 판은 1인칭 장면마다 손에 든 카메라 흉내(사인파 흔들기)를
  얹고, 붙잡히는 장면은 화면이 기울며 떨렸다. 보는 사람에게는 무섭기보다 어지러웠다.
  장면표의 흔들기를 지웠고, 촬영 동안 1인칭 시점의 걸음·숨 흔들림도 끈다. 움직임은 천천히
  다가가거나 돌아보는 것뿐이다. 접근성 지침도 카메라 흔들림과 모션 블러를 끌 수 있어야
  한다고 본다(Xbox Accessibility Guideline 117).
- **첫 화면부터 게임이다.** 검은 화면이나 제목으로 열지 않는다. 사람들은 처음 5초 안에 계속 볼지
  정한다. 403호에서 천장을 올려다보는 장면에 천장의 노크가 같이 온다.
- **소리 없이 자동 재생돼도 읽힌다.** 노크와 문밖의 목소리는 게임에서 실제로 뜨는 자막·대사
  상자로 함께 보여 준다.
- **실제 게임 화면만 쓴다.** 색 보정과 필름 효과는 얹지 않는다. 화면 질감은 게임 자체의 것이다.
- **붙잡히는 장면은 한 번에 찍힌 그대로 쓴다.** 기어 오는 몸, 발밑에서 붙잡힘, 손전등 빛 속으로
  떠오르는 얼굴, 바닥 노크에 끊김까지 끊지 않는다.
- **끝은 제목과 받는 곳이다.** ‘Windows 무료 플레이 테스트’와 저장소 주소를 남긴다.
- 이야기 후반의 장소와 결말은 보여 주지 않는다.

## 구성

| 시간 | 장면 | 소리와 글 |
|---|---|---|
| 0~6초 | 새벽 네 시 반, 403호에서 천장을 올려다본다 | 먹먹한 노크 셋, 자막 「[위] 천장에서 세 번 두드리는 소리」, 속말 「…4층이 꼭대기인데.」 |
| 6.6~24.4초 | 전날 저녁의 골목, 4층 복도, 402호 문의 전단, 관리실 CCTV | 도시 소리, 발소리, 안정기 |
| 25~32초 | 새벽의 403호 현관. 손전등으로 닫힌 철문을 비춘 채 천천히 다가간다 | 철문 노크 셋, 문밖의 웅얼거림, 대사 「언니, 저 편의점 나린이에요. 아까 지갑 두고 가셨어요.」 |
| 32~37초 | 불 꺼진 새벽의 복도를 걷는다 | 느린 발소리, 먼 노크 |
| 37~43초 | 등 하나만 남은 복도 끝의 어둑시니. 쳐다보는 동안 커지며 다가온다 | 바람 같은 숨, 가빠지는 숨 |
| 43.6~53.6초 | 4층 참의 위층 사람, 복도 끝에서 기어 와 붙잡는다 | 기는 걸음이 잦아지고, 낚아채는 소리, 바닥 노크 하나에 끊김, 검은 화면의 둘째 노크 |
| 53.6~62초 | 제목, Windows 무료 플레이 테스트, 저장소 주소 | 위에서 둘, 쉬고, 하나 |

## 촬영과 편집

Unreal Engine 5.8, Python(Pillow, numpy, scipy), ffmpeg가 필요하다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Build-ArtAssets.ps1 -CodeOnly
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Trailer/Build-Trailer.ps1
```

`Build-Trailer.ps1`은 새 실행 폴더 `Saved/Trailer/Runs/<시각>`에서 촬영, 글자 판, 편집표, 소리,
두 언어 영상, 배포용(`-hq`)과 README용(`-web`) 인코딩까지 한 번에 만든다. 끝으로 여섯 영상의 길이,
화면 크기, 초당 프레임, 소리 줄을 ffprobe로 확인하고, README용이 10MB 아래인지 본다. 이 확인을
통과해야 성공으로 기록된다.

실행 폴더는 자동 정리(`GeneratedArtifactCleanup.ps1`)에 등록된다. 하루가 지나고 최근 성공 두 번에
들지 않는 실행에서는 다시 찍으면 되는 장면 프레임과 중간 소리(`trailer_mix.wav`, `trailer_audio.wav`),
기록만 지운다. 영상, 정지 화면, 글자 판, 편집표, 음량 측정은 남는다. 실패한 실행도 마지막 한 번은
남는다. 예전 `Saved/Trailer`의 프레임과 영상은 등록하지 않으므로 자동으로 지워지지 않는다.

장면 몇 개만 다시 찍어 볼 때는 단계를 따로 돌린다. 이때는 예전처럼 `Saved/Trailer`에 쌓이고
자동 정리 밖이다. 다른 폴더에서 편집하려면 `IG_TRAILER_DIR`에 그 폴더를 준다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Trailer/Run-TrailerCapture.ps1 -Shot door-402,booth-cctv
python Scripts/Trailer/cards.py
python Scripts/Trailer/assemble.py --marks
python Scripts/Trailer/compose.py
python Scripts/Trailer/assemble.py ko
python Scripts/Trailer/assemble.py en
```

촬영기(`-IGTrailerCapture`)는 장면마다 시점을 옮기며 UI 없이 프레임을 한 장씩 남긴다.
장면표는 `AIGListenerGreyboxDirector::AdvanceTrailerCapture`에 있고, `-Shot door-402,booth-cctv`처럼
몇 장면만 다시 찍을 수 있다. 위층 사람은 녹화가 시작될 때 움직이기 시작하고, 그때 플레이어 자리에서
작은 소리를 내서 그가 살피러 오게 한다. 어둑시니 장면은 복도 등을 서쪽 끝 하나만 남기고 녹화 1초 전에
몸을 세운다. 기어 오는 장면은 전체 촬영 순서에서 찍는다. 단독으로 찍으면 장면 앞의 상태가 달라
그가 처음부터 가까이 있을 수 있다.

`assemble.py`의 `APPROACH_FIRST`·`APPROACH_GRAB`·`APPROACH_CUT`은 기어 오는 장면에서 쓸 첫 프레임,
붙잡히는 프레임, 끊기는 프레임이다. 다시 찍으면 프레임 밝기를 재서 고친다. 붙잡힌 뒤 손전등 빛이
꺼져 몇 프레임 어둡고, 얼굴이 바닥 빛 속으로 떠오른 뒤 다시 검어지는 순간이 끊김이다.

어둑시니 장면은 `FRAMING`에 적은 대로 복도 끝을 1.6배로 잘라 쓴다. 원래 화각에서는 등 앞에 선
형체가 작은 화면에서 몇 픽셀밖에 안 된다. 자른 화면은 고정이고 움직이지 않는다.

결과는 실행 폴더의 `MissingFloor-Trailer.mp4`(한국어)와 `MissingFloor-Trailer-en.mp4`(영어)다
(단계를 따로 돌리면 `Saved/Trailer`). 화면 잡티 때문에 원본이 크므로 배포용과 README용을 따로
인코딩한다. `Build-Trailer.ps1`은 아래 인코딩까지 해 둔다. 릴리스 묶음에는
`python Scripts/make_release_zip.py --trailer <실행 폴더>/MissingFloor-Trailer-hq.mp4`로 넘긴다.

```powershell
ffmpeg -i MissingFloor-Trailer.mp4 -c:v libx264 -preset slow -crf 22 -tune grain -c:a aac -b:a 160k -movflags +faststart MissingFloor-Trailer-hq.mp4
ffmpeg -i MissingFloor-Trailer.mp4 -vf "scale=1280:720:flags=lanczos,hqdn3d=2:1.5:3:3" -c:v libx264 -preset slow -b:v 1150k -pass 1 -an -f mp4 NUL
ffmpeg -i MissingFloor-Trailer.mp4 -vf "scale=1280:720:flags=lanczos,hqdn3d=2:1.5:3:3" -c:v libx264 -preset slow -b:v 1150k -pass 2 -c:a aac -b:a 96k -movflags +faststart MissingFloor-Trailer-web.mp4
```

README 맨 위 영상은 10MB 아래의 `-web` 파일을 GitHub 편집기에 끌어 넣어 올린다.

## 소리

게임의 WAV만 쓴다. 음악과 나레이션은 없다. 문밖의 목소리는 게임처럼 철문에 걸러진 웅얼거림을
합성하고, 말은 대사 상자가 맡는다. `compose.py`가 ffmpeg loudnorm을 두 번 돌려 -19 LUFS, 최대
-1.5 dBTP로 맞추고 측정값을 `Saved/Trailer/audio-loudness.json`에 남긴다.

## 배포 전 확인

- 자막과 대사가 게임의 실제 문구와 같은지 본다.
- 프레임이 빠지면(`TRAILER_CAPTURE PASS dropped=0`이 아니면) 편집하지 않는다.
- 붙잡히는 장면에 번쩍이는 프레임이 없는지 밝기로 잰다. 예전에는 튕겨 나간 손전등 빛이 코앞의
  얼굴을 쓸고 지나가 네 프레임이 하얗게 탔다.
- 작은 화면에서도 어둑시니의 실루엣과 문에 붙은 전단이 알아보이는지 본다.
- 릴리스에 배포용 MP4를 붙이고, README의 예고편 링크를 같은 버전으로 맞춘다.

## 참고

- Steam 예고편 권장 사항: 처음 5초, 로고·검은 화면으로 열지 않기, 자동 재생은 소리 없이, 끝의
  행동 유도. https://www.steampageanalyzer.com/blog/steam-trailer-best-practices
- 실제 게임 장면을 먼저 보여 줄 것. https://wnhub.io/news/analytics/item-237
- Xbox Accessibility Guideline 117(시각적 방해와 움직임 설정).
  https://learn.microsoft.com/gaming/accessibility/guidelines

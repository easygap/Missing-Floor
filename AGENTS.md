# Missing Floor — 작업 규칙

## 커밋 작성자

**커밋 작성자는 항상 easygap이다.** 예외 없다.

```
easygap <103491329+easygap@users.noreply.github.com>
```

커밋 메시지에 `Co-Authored-By`, `Codex-Session`, 그 밖에 AI 도구 이름이
들어가는 트레일러를 붙이지 마라. GitHub은 `Co-Authored-By`를 읽어서
저장소 기여자 목록을 만든다. 한 줄만 새어 나가도 기여자에 이름이 올라간다.

작업을 시작하기 전에 이걸 먼저 걸어라.

```
git config user.name easygap
git config user.email 103491329+easygap@users.noreply.github.com
git config core.hooksPath .githooks
```

컨테이너나 새 클론의 전역 git 설정이 다른 이름으로 되어 있는 경우가 있다.
로컬 설정이 전역을 이기므로 위 세 줄이면 덮인다.

`.githooks/commit-msg`가 마지막 그물이다. 트레일러나 작성자에 AI 도구
이름이 있으면 커밋 자체를 거부한다. `.githooks/`에는 LFS 훅 네 개도 같이
들어 있다. `core.hooksPath`를 바꾸면 `.git/hooks`가 통째로 무시되기 때문에,
여기서 빼면 LFS가 죽는다.

### 한 번만 해 두면 좋은 것

`.Codex/settings.json`은 저장소에 커밋할 수 없다(에이전트 세션에서
막힌다). 로컬에 직접 만들어 두면 트레일러가 애초에 생성되지 않는다.

```json
{
  "attribution": { "commit": "", "pr": "", "sessionUrl": false }
}
```

## 문서와 주석

주석, 커밋 메시지, 문서는 한국어로 쓴다. 번역체 말고 사람이 쓴 문장으로.

- 영문을 직역한 티가 나는 표현을 쓰지 마라
- 같은 말을 두 번 강조하지 마라. 굵게는 정말 필요한 자리에만
- "~가 아니라 ~입니다" 같은 대구를 반복해서 쓰지 마라
- 당연한 걸 굳이 방어적으로 설명하지 마라

## README

README는 개발 노트가 아니라 **게임을 처음 보는 사람이 읽는 문서**다.

- 최근에 만든 기능을 따로 섹션으로 세워서 자랑하듯 넣지 마라
- Star나 Watch를 눌러 달라는 문구를 넣지 마라
- 첫 화면은 한국어다. 영어·일본어·중국어 간체·번체 소개는 `Docs/Readme/`에
  두고, 모든 소개 맨 위에서 다른 언어로 이동할 수 있게 한다.
  이 다국어 소개는 각 언어로 쓴다. 한국어 문장 순서를 그대로 옮기지 마라.
- 소개 페이지의 언어와 게임이 실제로 지원하는 언어를 구분해 안내한다.
- 화면 캡처와 GIF를 충분히 쓰되, 첫 화면 무게를 보고 결정해라
- 무거운 캡처는 원본을 `Docs/Media/`에 두고
  `Scripts/optimize_readme_media.py`로 `Docs/Media/readme/`에 축소본을 만든다.
  원본을 그대로 인라인하면 README 여는 데 수십 MB를 받는다
- `Scripts/Validate-Project.ps1`이 README의 필수 토큰과 이미지 링크,
  GIF 용량 범위를 검사한다. 고치고 나면 돌려 봐라

## 검증

```powershell
pwsh -NoProfile -File Scripts/Validate-Project.ps1
```

리눅스에서는 `System.Drawing.Common`을 쓰는 검사 네 개가 못 돈다.
.NET 6부터 윈도우 전용이고, 그 검사들은 맑은 고딕 글자 폭을 재는 것이라
리눅스에서는 의미 자체가 없다. 임시 파일 자동 정리 검사도 Windows 파일 잠금을 다루므로
리눅스에서 건너뛴다. 나머지는 전부 돈다.

## 임시 산출물 자동 정리

빌드·검증 스크립트는 `Scripts/GeneratedArtifactCleanup.ps1`의 실행 잠금을 잡고,
새 결과 폴더를 등록한 뒤 `finally`에서 정리를 마친다. 새 스크립트에도 같은 연결을 넣는다.
원본·세이브·최신 배포본은 정리 대상에 넣지 않는다. 보관 기준과 예외는
[임시 파일 정리](Docs/GENERATED_FILE_CLEANUP.md)를 따른다. 정리 로직을 바꾸면
`Scripts/Test-GeneratedArtifactCleanup.ps1`을 실행한다.

## 에셋 파이프라인

`Content/**`, `Docs/Media/**`의 이미지는 Git LFS에 있다. 클론 직후에는
`git lfs pull`을 먼저 돌려라. 안 그러면 캡처가 포인터 파일이라 해시 검사가
전부 깨진다.

# Missing Floor 실행 안내

## Windows에서 플레이하기

1. [Windows 테스트 버전](https://github.com/easygap/Missing-Floor/releases/tag/v0.9.1)에서 `MissingFloor-0.9.1-Windows.zip`을 받습니다.
2. ZIP 파일의 **압축을 모두 풉니다.** 실행 파일 옆의 `Engine`, `IndieGame` 폴더도 함께 있어야 합니다.
3. `MissingFloor.exe`를 실행하고 **게임 시작**을 선택합니다.
4. 소리 크기와 밝기를 맞춘 뒤 시작합니다. 조작법은 `F1`, 난이도와 접근성 설정은 `F10`으로 다시 열 수 있습니다.

게임은 처음 켤 때 Windows 언어를 따릅니다. 한국어, 영어, 일본어, 중국어(간체, 번체) 가운데
고를 수 있고, 타이틀의 **설정 > 일반 > 언어 · Language**에서 바로 바꿀 수 있습니다.

Unreal Engine이나 Visual Studio를 설치할 필요는 없습니다.
저장된 진행이 있으면 타이틀 화면의 **이어하기**로 계속할 수 있습니다.

![Missing Floor 타이틀 화면](Media/readme/title-menu-first-run-1080.webp)

Windows 11, Ryzen 9 7900X, RTX 3060, 메모리 32GB PC에서 실행을 확인했습니다.
다른 일반 PC의 화면·성능은 아직 확인하지 않아 최소 사양은 정하지 않았습니다.

## 실행이 안 될 때

- **DLL이 없다고 나올 때:** 압축을 모두 풀었는지 먼저 확인해 주세요. 같은 폴더의 `Engine/Extras/Redist/en-us/vc_redist.x64.exe`로 필요한 실행 구성 요소를 설치할 수 있습니다.
- **화면이 끊길 때:** 설정에서 그래픽 품질과 해상도를 낮춰 보세요.
- **소리가 작을 때:** 게임의 전체 소리와 Windows 볼륨 믹서를 확인해 주세요. 배경 음악과 환경음도 따로 조절할 수 있습니다.

문제가 계속되면 [이슈](https://github.com/easygap/Missing-Floor/issues)에
오류 메시지, Windows 버전, 그래픽카드와 문제가 생긴 장면을 남겨 주세요.

## 소스에서 빌드하기

직접 수정하거나 빌드하려면 Unreal Engine 5.8, Visual Studio의 **C++를 사용한 게임 개발** 도구와 Windows SDK, Git LFS가 필요합니다. 스크립트는 PowerShell 7(`pwsh`)과 Windows에 기본으로 들어 있는 Windows PowerShell(`powershell -ExecutionPolicy Bypass -File ...`) 모두에서 돌아갑니다.

```powershell
git lfs install
git clone https://github.com/easygap/Missing-Floor.git
cd Missing-Floor
git lfs pull
pwsh -NoProfile -File .\Scripts\Build-ArtAssets.ps1 -CodeOnly
.\Scripts\RunGame.bat
```

게임 데이터까지 묶은 Windows 배포 파일은 다음 명령으로 만듭니다.

```powershell
pwsh -NoProfile -File .\Scripts\Package-Windows.ps1
```

완료되면 배포 폴더 경로가 출력됩니다. `Windows/MissingFloor.exe`가 실행 파일입니다.

패키징이 끝나면 파일 해시, 설치 용량, 실행 구성 요소, 폰트와 사용 허가문을
자동으로 확인합니다. 실제 게임을 켜서 화면·입력, 저장·이어하기, 전체 진행과
소리까지 검사하려면 출력된 경로를 다음 명령에 넣습니다.

```powershell
pwsh -NoProfile -File .\Scripts\Run-WindowsPackageReview.ps1 -ArchiveDirectory '배포 폴더 경로'
```

출시 후보를 만들 때는 변경 사항을 모두 커밋하고 두 명령에 `-RequireCleanCommit`을
붙입니다. 다른 PC에서 검사할 때는 `-ExpectedCommit`에 검토 중인 40자리 커밋 값을
넣으면 후보가 뒤섞이는 일을 막을 수 있습니다. 자동 검사를 통과해도
[성능 검증](PERFORMANCE.md)의 장비별 측정과 사람이 직접 처음부터 끝까지
플레이하는 검수는 따로 필요합니다.

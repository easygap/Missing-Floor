[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'GeneratedArtifactCleanup.ps1')
. (Join-Path $PSScriptRoot 'Resolve-AsciiWorkspace.ps1')

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    Write-Host 'ASCII_WORKSPACE_TEST SKIP Windows junction 검사입니다.'
    return
}

$projectRoot = Get-IGPhysicalPath (Join-Path $PSScriptRoot '..')
$scope = Enter-IGArtifactScope -ProjectRoot $projectRoot -Name 'Test-AsciiWorkspace.ps1'
$succeeded = $false
$checks = 0
$links = [Collections.Generic.List[string]]::new()
$runRelative = 'Saved/Validation/AsciiWorkspace-' + [Guid]::NewGuid().ToString('N')
$outputRoot = Join-Path $projectRoot $runRelative
$fixtureRoot = Join-Path $outputRoot 'Fixtures'

function Assert-Workspace([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "ASCII 작업 경로 검사 실패: $Message" }
    $script:checks++
}
function Assert-Rejected([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Assert-Workspace $rejected $Message
}
function Write-Fixture([string]$Path, [string]$Value) {
    $parent = New-IGPhysicalDirectory (Split-Path -Parent $Path)
    [IO.File]::WriteAllText((Join-Path $parent (Split-Path -Leaf $Path)), $Value, [Text.UTF8Encoding]::new($false))
}
function New-TestJunction([string]$Path, [string]$Target) {
    $parent = New-IGPhysicalDirectory (Split-Path -Parent $Path)
    New-Item -ItemType Junction -Path (Join-Path $parent (Split-Path -Leaf $Path)) -Target $Target | Out-Null
    $links.Add($Path)
}
function Resolve-TestWorkspace([string]$Project, [string]$LinkRoot) {
    $result = Resolve-IGAsciiWorkspace -ProjectRoot $Project -LinkRoot $LinkRoot
    if (-not $links.Contains($result)) { $links.Add($result) }
    return $result
}

try {
    Add-IGArtifactDirectory -Scope $scope -Directory $outputRoot
    [void][IO.Directory]::CreateDirectory($fixtureRoot)
    $originalProject = Join-Path $projectRoot 'IndieGame.uproject'
    $originalHash = (Get-FileHash -LiteralPath $originalProject -Algorithm SHA256).Hash
    $workspace = Resolve-IGAsciiWorkspace -ProjectRoot $projectRoot
    Assert-Workspace ($workspace -notmatch '[^\x00-\x7F]') '반환 경로에 비 ASCII 문자가 있습니다.'
    Assert-Workspace ((Get-IGPhysicalPath $workspace) -ieq (Get-IGPhysicalPath $projectRoot)) '다른 실제 작업 폴더입니다.'

    # 링크 자체는 ASCII 이름에 두고, 대상과 원본 파일에는 한글 이름을 사용한다.
    $aliasFixture = Join-Path (Join-Path $workspace $runRelative) 'Fixtures'
    $sourceA = Join-Path $fixtureRoot '첫째 프로젝트'
    $sourceB = Join-Path $fixtureRoot '둘째 프로젝트'
    Write-Fixture (Join-Path $sourceA 'IndieGame.uproject') '{"fixture":"A"}'
    Write-Fixture (Join-Path $sourceB 'IndieGame.uproject') '{"fixture":"B"}'
    Write-Fixture (Join-Path $sourceA 'Content/원본.txt') '남겨야 하는 원본'
    Write-Fixture (Join-Path $sourceA 'Intermediate/캐시.txt') '공유할 중간 결과'
    Write-Fixture (Join-Path $sourceA 'Binaries/Win64/fixture.dll') '공유할 모듈'
    $sourceHash = (Get-FileHash -LiteralPath (Join-Path $sourceA 'Content/원본.txt') -Algorithm SHA256).Hash
    Assert-Workspace (Test-Path -LiteralPath $aliasFixture -PathType Container) "ASCII 별칭에서 검사 폴더를 찾지 못했습니다: $aliasFixture"

    $freshRoot = Join-Path $aliasFixture 'FreshLinks'
    $fresh = Resolve-TestWorkspace $sourceA $freshRoot
    $same = Resolve-TestWorkspace $sourceA $freshRoot
    Assert-Workspace ($fresh -eq $same) '같은 작업 폴더에 새 경로를 반복 생성했습니다.'
    Assert-Workspace ((Resolve-TestWorkspace $fresh $freshRoot) -eq $fresh) '별칭으로 들어오면 중복 경로가 생깁니다.'
    foreach ($relative in @('Intermediate/캐시.txt', 'Binaries/Win64/fixture.dll', 'Content/원본.txt')) {
        Assert-Workspace ((Get-IGPhysicalPath (Join-Path $fresh $relative)) -ieq
            (Get-IGPhysicalPath (Join-Path $sourceA $relative))) "원본과 다른 파일입니다: $relative"
    }
    [IO.File]::WriteAllText((Join-Path $fresh 'Binaries/Win64/new-module.dll'), '별칭으로 쓴 모듈')
    Assert-Workspace (Test-Path -LiteralPath (Join-Path $sourceA 'Binaries/Win64/new-module.dll')) '새 파일이 원본에 바로 나타나지 않습니다.'

    $foreignRoot = Join-Path $aliasFixture 'ForeignLinks'
    $foreign = Join-Path $foreignRoot 'WorkspaceLink'
    New-TestJunction $foreign $sourceB
    $fallback = Resolve-TestWorkspace $sourceA $foreignRoot
    Assert-Workspace ((Split-Path -Leaf $fallback) -like 'WorkspaceLink_*') '다른 저장소의 공용 링크를 재사용했습니다.'
    Assert-Workspace ((Get-IGPhysicalPath $foreign) -ieq (Get-IGPhysicalPath $sourceB)) '다른 저장소의 링크를 바꿨습니다.'
    Assert-Rejected { Assert-IGWorkspaceJunction -Path $foreign -PhysicalRoot (Get-IGPhysicalPath $sourceA) } '잘못된 링크 대상을 허용했습니다.'

    $blockedRoot = Join-Path $aliasFixture 'BlockedLinks'
    New-TestJunction (Join-Path $blockedRoot 'WorkspaceLink') $sourceB
    $blockedHash = Join-Path $blockedRoot (Split-Path -Leaf $fallback)
    New-TestJunction $blockedHash $sourceB
    Assert-Rejected { Resolve-IGAsciiWorkspace -ProjectRoot $sourceA -LinkRoot $blockedRoot } '해시 경로의 잘못된 링크를 허용했습니다.'
    Assert-Workspace ((Get-IGPhysicalPath $blockedHash) -ieq (Get-IGPhysicalPath $sourceB)) '거부한 링크를 바꿨습니다.'

    $realRoot = Join-Path $aliasFixture 'RealDirectories'
    $realPreferred = Join-Path $realRoot 'WorkspaceLink'
    Write-Fixture (Join-Path $realPreferred 'keep.txt') '기존 실제 폴더'
    $realFallback = Resolve-TestWorkspace $sourceA $realRoot
    Assert-Workspace ($realFallback -ne $realPreferred) '실제 폴더를 작업 링크로 썼습니다.'
    Assert-Workspace ([IO.File]::ReadAllText((Join-Path $realPreferred 'keep.txt')) -eq '기존 실제 폴더') '기존 실제 폴더를 바꿨습니다.'
    $realBlockedRoot = Join-Path $aliasFixture 'RealBlocked'
    Write-Fixture (Join-Path $realBlockedRoot 'WorkspaceLink/keep.txt') '공용 이름 점유'
    $realHash = Join-Path $realBlockedRoot (Split-Path -Leaf $fallback)
    Write-Fixture (Join-Path $realHash 'keep.txt') '해시 이름 점유'
    Assert-Rejected { Resolve-IGAsciiWorkspace -ProjectRoot $sourceA -LinkRoot $realBlockedRoot } '해시 경로의 실제 폴더를 허용했습니다.'
    Assert-Workspace ([IO.File]::ReadAllText((Join-Path $realHash 'keep.txt')) -eq '해시 이름 점유') '거부한 실제 폴더를 바꿨습니다.'
    Assert-Workspace ((Get-FileHash -LiteralPath (Join-Path $sourceA 'Content/원본.txt') -Algorithm SHA256).Hash -eq $sourceHash) '원본 내용이 바뀌었습니다.'
    Assert-Workspace ((Get-FileHash -LiteralPath $originalProject -Algorithm SHA256).Hash -eq $originalHash) '실제 프로젝트 파일이 바뀌었습니다.'

    foreach ($name in @('Build-ArtAssets.ps1', 'Localize-Game.ps1', 'Package-Windows.ps1', 'Record-PsoCache.ps1')) {
        $text = [IO.File]::ReadAllText((Join-Path $PSScriptRoot $name))
        Assert-Workspace ($text.Contains('Resolve-AsciiWorkspace.ps1') -and $text.Contains('Resolve-IGAsciiWorkspace -ProjectRoot')) "공통 경로 검증을 사용하지 않습니다: $name"
        Assert-Workspace ($text -notmatch 'AsciiBuild|usingAsciiMirror|Invoke-ArtRobocopy|Sync-Folder|robocopy|/MIR') "복사 또는 미러 경로가 남았습니다: $name"
    }
    [pscustomobject]@{ checks = $checks; workspace = $workspace; physicalRoot = (Get-IGPhysicalPath $workspace); originalProjectSha256 = $originalHash } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputRoot 'summary.json') -Encoding utf8
    $succeeded = $true
    Write-Host "ASCII_WORKSPACE_TEST PASS assertions=$checks shared_intermediate=true shared_binaries=true copied_project_files=0"
}
finally {
    try {
    # 직접 만든 링크만 비재귀로 해제한 뒤, 검사용 폴더 안의 작은 원본을 정리한다.
    foreach ($link in $links) {
        $item = Get-Item -LiteralPath $link -Force -ErrorAction SilentlyContinue
        if ($null -eq $item) { continue }
        if ($item.LinkType -ne 'Junction' -or
            -not $link.StartsWith($aliasFixture + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "검사용 링크의 경계를 확인하지 못했습니다: $link"
        }
        $target = Get-IGPhysicalPath $link
        if (-not $target.StartsWith($fixtureRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "검사용 링크가 밖을 가리킵니다: $link"
        }
        Remove-Item -LiteralPath $link -Force
    }
    if ((Test-Path -LiteralPath $fixtureRoot) -and (Test-IGArtifactPath $fixtureRoot $outputRoot)) {
        if (@(Get-ChildItem -LiteralPath $fixtureRoot -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count -eq 0) {
            Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
        }
    }
    }
    finally { Complete-IGArtifactScope -Scope $scope -Succeeded $succeeded }
}

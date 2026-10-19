# 빌드·검증에서 새로 만든 폴더만 등록한다. 기존 자료를 찾아서 소유권을 가져오지 않는다.
# 실행 중에는 공유 잠금을 잡고, 정리는 모든 실행이 끝난 뒤 독점 잠금으로 한다.

function Test-IGArtifactPath {
    param([string]$Path, [string]$Parent)
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $base = [IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    if (-not $full.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { return $false }
    $cursor = $full
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            if ((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { return $false }
        }
        $cursor = Split-Path -Parent $cursor
    }
    return $true
}

function Get-IGArtifactKind {
    param([string]$ProjectRoot, [string]$Directory)
    # 예고편은 Build-Trailer.ps1이 실행마다 새로 만드는 Saved/Trailer/Runs 아래만 맡는다. 예전 Saved/Trailer는 손대지 않는다.
    foreach ($pair in @(@('Validation', 'Saved/Validation'), @('Package', 'Saved/Packages'), @('Pso', 'Saved/PsoRecord'),
        @('Trailer', 'Saved/Trailer/Runs'))) {
        if (Test-IGArtifactPath $Directory (Join-Path $ProjectRoot $pair[1])) { return $pair[0] }
    }
    if (Test-IGArtifactPath $Directory $ProjectRoot) { return '' }
    # 별도 드라이브에 두는 전용 빌드·배포 검사 폴더도 새 실행부터 같은 규칙을 쓴다.
    $parent = Split-Path -Parent ([IO.Path]::GetFullPath($Directory).TrimEnd('\', '/'))
    if (-not (Test-IGArtifactPath $Directory $parent)) { return '' }
    if ((Split-Path -Leaf $parent) -eq 'MissingFloorBuild') { return 'Package' }
    if ((Split-Path -Leaf $parent) -eq 'MissingFloorSmoke') { return 'Smoke' }
    if ($env:LOCALAPPDATA -and $parent -ieq (Join-Path $env:LOCALAPPDATA 'IndieGame/ReviewPackages')) { return 'Package' }
    return ''
}

function Get-IGArtifactStore {
    param([string]$ProjectRoot)
    $root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\', '/')
    $store = Join-Path $root 'Saved/ArtifactCleanup'
    if (-not (Test-Path -LiteralPath (Join-Path $root 'IndieGame.uproject') -PathType Leaf) -or
        -not (Test-IGArtifactPath $store $root)) { throw '자동 정리 기록 폴더의 경로를 확인하지 못했습니다.' }
    [void][IO.Directory]::CreateDirectory($store)
    return $store
}

function Open-IGArtifactLock {
    param([string]$Store, [switch]$Exclusive)
    $path = Join-Path $Store 'activity.lock'
    $share = if ($Exclusive) { [IO.FileShare]::None } else { [IO.FileShare]::Read }
    $until = [DateTime]::UtcNow.AddSeconds(30)
    do {
        try { return [IO.File]::Open($path, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::Read, $share) }
        catch [IO.IOException] {
            if ($Exclusive) { return $null }
            if ([DateTime]::UtcNow -ge $until) { throw '임시 파일 정리가 끝나기를 기다리다 시간이 초과됐습니다. 잠시 뒤 다시 실행해 주세요.' }
            Start-Sleep -Milliseconds 100
        }
    } while ($true)
}

function Enter-IGArtifactScope {
    param([string]$ProjectRoot, [string]$Name, [System.Collections.IDictionary]$Parameters = @{})
    $store = Get-IGArtifactStore $ProjectRoot
    $options = [ordered]@{}
    foreach ($key in @($Parameters.Keys | Sort-Object)) {
        # 결과 위치나 제한 시간만 바뀐 실행은 같은 검사로 묶는다.
        if ($key -match 'Directory|Path|Archive|Timeout|ExpectedCommit|RequireClean|SeedSave|ReleaseTag') { continue }
        $options[$key] = [string]($Parameters[$key] -join ',')
    }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($options | ConvertTo-Json -Compress)))).Replace('-', '') }
    finally { $sha.Dispose() }
    $usedPaths = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($key in $Parameters.Keys) {
        if ($key -notmatch 'Directory|Path|Archive|SeedSave') { continue }
        foreach ($value in @($Parameters[$key])) {
            if ($value -isnot [string] -or [string]::IsNullOrWhiteSpace($value)) { continue }
            if (Test-Path -LiteralPath $value -ErrorAction SilentlyContinue) {
                $resolved = Resolve-Path -LiteralPath $value -ErrorAction Stop
                if ($resolved.Provider.Name -eq 'FileSystem') { [void]$usedPaths.Add($resolved.ProviderPath.TrimEnd('\', '/')) }
            }
        }
    }
    $scope = [pscustomobject]@{
        ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\', '/')
        Store = $store; Lease = (Open-IGArtifactLock $store)
        Group = "$Name/$digest"; Runs = [Collections.Generic.List[object]]::new(); UsedPaths = $usedPaths
    }
    try { Update-IGArtifactUsage $scope }
    catch { Write-Warning "이전 결과의 사용 시각을 기록하지 못했습니다: $($_.Exception.Message)" }
    return $scope
}

function Update-IGArtifactUsage {
    param($Scope)
    if (-not $Scope.UsedPaths.Count) { return }
    foreach ($file in Get-ChildItem -LiteralPath $Scope.Store -Filter '*.json' -File) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
        # 다른 실행이 기록 중인 목록은 아직 정리 대상이 아니다.
        try { $record = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json }
        catch { continue }
        if ($record.schema -ne 1 -or $record.id -notmatch '^[0-9a-f]{32}$' -or $file.BaseName -cne $record.id -or
            $record.project -ine $Scope.ProjectRoot -or -not (Get-IGArtifactKind $Scope.ProjectRoot $record.directory)) { continue }
        foreach ($path in $Scope.UsedPaths) {
            if ($path -ieq $record.directory -or (Test-IGArtifactPath $path $record.directory) -or
                (Test-IGArtifactPath $record.directory $path)) {
                $marker = Join-Path $Scope.Store ($record.id + '.use')
                if (-not (Test-IGArtifactPath $marker $Scope.Store)) { throw '사용 시각 기록의 경로가 바뀌었습니다.' }
                # 완료 목록을 다시 쓰지 않아 동시에 같은 빌드를 검사해도 기록이 깨지지 않는다.
                $stream = [IO.File]::Open($marker, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::Write, [IO.FileShare]::ReadWrite)
                try { $stream.WriteByte(1) }
                finally { $stream.Dispose() }
                break
            }
        }
    }
}

function Add-IGArtifactDirectory {
    param($Scope, [string]$Directory, [string]$Variant = '')
    $full = [IO.Path]::GetFullPath($Directory).TrimEnd('\', '/')
    $kind = Get-IGArtifactKind $Scope.ProjectRoot $full
    # 사용자가 지정한 외부 배포 경로와 이미 내용이 있는 폴더는 자동 정리하지 않는다.
    if (-not $kind) { return }
    $parent = Split-Path -Parent $full
    while (Test-IGArtifactPath $parent $Scope.ProjectRoot) {
        if (Test-Path -LiteralPath (Join-Path $parent '.ig-artifact-owner')) { return }
        $parent = Split-Path -Parent $parent
    }
    if ((Test-Path -LiteralPath $full) -and @(Get-ChildItem -LiteralPath $full -Force).Count) {
        [void]$Scope.UsedPaths.Add($full)
        return
    }
    [void][IO.Directory]::CreateDirectory($full)
    $id = [Guid]::NewGuid().ToString('N')
    $owner = [IO.File]::Open((Join-Path $full '.ig-artifact-owner'), [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $bytes = [Text.Encoding]::UTF8.GetBytes($id); $owner.Write($bytes, 0, $bytes.Length) }
    finally { $owner.Dispose() }
    $Scope.Runs.Add([pscustomobject]@{ Id = $id; Directory = $full; Kind = $kind; Group = "$kind/$($Scope.Group)/$Variant" })
}

function Test-IGArtifactFile {
    param([string]$RelativePath, [string]$Kind)
    $path = $RelativePath.Replace('\', '/')
    if ([IO.Path]::IsPathRooted($path) -or $path.Contains(':') -or $path.Split('/') -contains '..' -or
        $path -match '(^|/)(SaveGames|Saves|Autosaves|Backups?|Originals?|Source|SourceArt|\.git|Docs)(/|$)' -or
        $path -match '\.(sav|bak|blend[0-9]*|psd|kra|uproject|uasset|umap)(\.|$)') { return $false }
    $extension = [IO.Path]::GetExtension($path).ToLowerInvariant()
    if ($Kind -eq 'Package') {
        return ($path.StartsWith('Windows/', [StringComparison]::OrdinalIgnoreCase) -and $extension -in
            '.exe', '.dll', '.pdb', '.pak', '.ucas', '.utoc', '.bin', '.dat', '.locres', '.ttf', '.otf', '.upipelinecache')
    }
    if ($Kind -eq 'Trailer') {
        # 다시 찍으면 되는 장면 프레임과 중간 소리, 기록만 지운다. 영상·정지 화면·글자 판·편집표는 남긴다.
        return ($path -match '^[a-z0-9-]+/frame_[0-9]{4}\.png$' -or $path -match '^trailer_(mix|audio)\.wav$' -or
            ($extension -eq '.log' -and $path -notmatch '/'))
    }
    if ($Kind -eq 'Smoke' -and $path -match '^(Download|Game)/' -and $extension -in
        '.zip', '.exe', '.dll', '.pdb', '.pak', '.ucas', '.utoc', '.bin', '.dat', '.locres', '.ttf', '.otf') { return $true }
    return ($extension -in '.png', '.jpg', '.jpeg', '.gif', '.bmp', '.mp4', '.wav', '.csv', '.etl', '.utrace',
        '.cache', '.tmp', '.log', '.upipelinecache', '.spc', '.shk')
}

function Get-IGArtifactFiles {
    param([string]$Directory)
    $pending = [Collections.Generic.Stack[string]]::new()
    $pending.Push($Directory)
    while ($pending.Count) {
        foreach ($entry in Get-ChildItem -LiteralPath $pending.Pop() -Force -ErrorAction Stop) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
            if ($entry.PSIsContainer) { $pending.Push($entry.FullName) } else { $entry }
        }
    }
}

function Get-IGArtifactProcesses {
    # 조회에 실패하면 호출한 쪽에서 정리를 건너뛴다.
    @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object { $_.ProcessId -ne $PID } | ForEach-Object {
        (([string]$_.ExecutablePath) + ' ' + ([string]$_.CommandLine)).Replace('/', '\')
    })
}

function Test-IGArtifactInUse {
    param([string]$Directory, [string[]]$Processes)
    foreach ($process in $Processes) {
        if ($process.IndexOf($Directory.Replace('/', '\'), [StringComparison]::OrdinalIgnoreCase) -ge 0) { return $true }
    }
    return $false
}

function Complete-IGArtifactScope {
    param($Scope, [bool]$Succeeded)
    $canCleanup = $false
    try {
        # 열린 파일 확인은 Windows 공유 모드에 의존한다. 다른 운영체제에서는 보존한다.
        if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { return }
        Update-IGArtifactUsage $Scope
        $processes = @(Get-IGArtifactProcesses)
        foreach ($run in $Scope.Runs) {
            if (-not (Get-IGArtifactKind $Scope.ProjectRoot $run.Directory) -or
                (Test-IGArtifactInUse $run.Directory $processes)) { continue }
            $owner = Join-Path $run.Directory '.ig-artifact-owner'
            if (-not (Test-Path -LiteralPath $owner -PathType Leaf) -or [IO.File]::ReadAllText($owner) -cne $run.Id) { continue }
            $files = @(foreach ($file in Get-IGArtifactFiles $run.Directory) {
                $relative = $file.FullName.Substring($run.Directory.Length + 1).Replace('\', '/')
                if (-not (Test-IGArtifactFile $relative $run.Kind)) { continue }
                [ordered]@{ path = $relative; bytes = $file.Length; ticks = $file.LastWriteTimeUtc.Ticks;
                    sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256 -ErrorAction Stop).Hash }
            })
            $record = [ordered]@{ schema = 1; id = $run.Id; project = $Scope.ProjectRoot; directory = $run.Directory;
                kind = $run.Kind; group = $run.Group; succeeded = $Succeeded;
                finishedUtc = [DateTime]::UtcNow.ToString('o'); files = $files }
            [IO.File]::WriteAllText((Join-Path $Scope.Store ($run.Id + '.json')), ($record | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
        }
        $canCleanup = $true
    }
    catch { Write-Warning "자동 정리 목록을 만들지 못해 결과를 보존했습니다: $($_.Exception.Message)" }
    finally { $Scope.Lease.Dispose() }
    if (-not $canCleanup) { return }
    try {
        $result = Invoke-IGArtifactCleanup -ProjectRoot $Scope.ProjectRoot
        if ($result.DeletedFiles) { Write-Host "ARTIFACT_CLEANUP files=$($result.DeletedFiles) bytes=$($result.DeletedBytes)" }
    }
    catch { Write-Warning "임시 파일 정리를 건너뛰었습니다: $($_.Exception.Message)" }
}

function Invoke-IGArtifactCleanup {
    [CmdletBinding(SupportsShouldProcess)]
    param([Parameter(Mandatory)][string]$ProjectRoot)
    $result = [pscustomobject]@{ DeletedFiles = 0; DeletedBytes = [long]0; Busy = $false }
    if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) { return $result }
    $store = Get-IGArtifactStore $ProjectRoot
    $lease = Open-IGArtifactLock $store -Exclusive
    if ($null -eq $lease) { $result.Busy = $true; return $result }
    try {
        $processes = @(Get-IGArtifactProcesses)
        $records = @(foreach ($file in Get-ChildItem -LiteralPath $store -Filter '*.json' -File) {
            try {
                if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { continue }
                $record = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
                $kind = Get-IGArtifactKind $ProjectRoot $record.directory
                if ($record.schema -ne 1 -or $record.id -notmatch '^[0-9a-f]{32}$' -or $file.BaseName -cne $record.id -or
                    $record.project -ine $ProjectRoot.TrimEnd('\', '/') -or
                    -not $kind -or $record.kind -cne $kind) { continue }
                $owner = Join-Path $record.directory '.ig-artifact-owner'
                if (-not (Test-IGArtifactPath $owner $record.directory) -or -not (Test-Path -LiteralPath $owner -PathType Leaf) -or
                    [IO.File]::ReadAllText($owner) -cne $record.id) { continue }
                $record
            }
            catch { Write-Warning "읽을 수 없는 자동 정리 기록을 보존했습니다: $($file.Name)" }
        })
        $retained = [Collections.Generic.HashSet[string]]::new()
        foreach ($group in $records | Group-Object group) {
            foreach ($record in @($group.Group | Where-Object { $_.succeeded } | Sort-Object { [DateTime]$_.finishedUtc } -Descending | Select-Object -First 2)) { [void]$retained.Add($record.id) }
            foreach ($record in @($group.Group | Where-Object { -not $_.succeeded } | Sort-Object { [DateTime]$_.finishedUtc } -Descending | Select-Object -First 1)) { [void]$retained.Add($record.id) }
        }
        $deadline = [DateTime]::UtcNow.AddSeconds(10)
        foreach ($record in $records) {
            if ([DateTime]::UtcNow -ge $deadline) { break }
            $usage = Join-Path $store ($record.id + '.use')
            if ((Test-Path -LiteralPath $usage) -and
                (-not (Test-IGArtifactPath $usage $store) -or (Get-Item -LiteralPath $usage -Force).LastWriteTimeUtc -gt [DateTime]::UtcNow.AddHours(-24))) { continue }
            if ($retained.Contains($record.id) -or [DateTime]$record.finishedUtc -gt [DateTime]::UtcNow.AddHours(-24) -or
                (Test-Path -LiteralPath (Join-Path $record.directory '.keep')) -or
                (Test-IGArtifactInUse $record.directory $processes)) { continue }
            # 중첩 검사 폴더를 따로 등록한 오래된 기록이 있어도, 보관할 부모·자식을 침범하지 않는다.
            $overlap = @($records | Where-Object { $retained.Contains($_.id) -and
                ((Test-IGArtifactPath $record.directory $_.directory) -or (Test-IGArtifactPath $_.directory $record.directory)) })
            if ($overlap.Count) { continue }
            $pending = $false
            foreach ($entry in $record.files) {
                if ([DateTime]::UtcNow -ge $deadline) { $pending = $true; break }
                if (-not (Test-IGArtifactFile $entry.path $record.kind)) { continue }
                $path = [IO.Path]::GetFullPath((Join-Path $record.directory $entry.path))
                if (-not (Test-IGArtifactPath $path $record.directory) -or -not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
                $file = Get-Item -LiteralPath $path -Force
                if ($file.Length -ne $entry.bytes -or $file.LastWriteTimeUtc.Ticks -ne $entry.ticks) { continue }
                $stream = $null
                $sha = $null
                try {
                    # 열린 파일은 건너뛴다. 검사부터 삭제까지 새 읽기·쓰기가 끼어들 수 없게 잡는다.
                    $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Delete)
                    $sha = [Security.Cryptography.SHA256]::Create()
                    $hash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '')
                    if ($hash -cne $entry.sha256) { continue }
                    if ($PSCmdlet.ShouldProcess($path, '완료된 실행의 오래된 임시 파일 삭제')) {
                        Remove-Item -LiteralPath $path -ErrorAction Stop
                        $stream.Dispose(); $stream = $null
                        if (Test-Path -LiteralPath $path) { throw '삭제 결과 확인 실패' }
                        $result.DeletedBytes += [long]$entry.bytes
                        $result.DeletedFiles++
                    }
                    else { $pending = $true }
                }
                catch { $pending = $true }
                finally { if ($sha) { $sha.Dispose() }; if ($stream) { $stream.Dispose() } }
            }
            if (-not $pending -and -not $WhatIfPreference) {
                # 세이브·요약 기록과 목록에 없던 파일은 그대로 남긴다. 폴더 전체 삭제는 하지 않는다.
                Remove-Item -LiteralPath (Join-Path $store ($record.id + '.json')) -ErrorAction Stop
                Remove-Item -LiteralPath (Join-Path $record.directory '.ig-artifact-owner') -ErrorAction Stop
                if (Test-Path -LiteralPath $usage) { Remove-Item -LiteralPath $usage -ErrorAction Stop }
            }
        }
    }
    finally { $lease.Dispose() }
    return $result
}

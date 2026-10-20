# 한글 경로를 읽지 못하는 빌드 도구에 같은 작업 폴더의 ASCII 경로를 제공한다.
# 파일을 복사하거나 기존 경로를 지우지 않는다. 링크의 실제 대상은 사용 전에 확인한다.

function Get-IGPhysicalPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not ('IGAsciiWorkspace.NativePath' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace IGAsciiWorkspace {
    public static class NativePath {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern SafeFileHandle CreateFileW(string name, uint access, uint share,
            IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern uint GetFinalPathNameByHandleW(SafeFileHandle file,
            StringBuilder path, uint length, uint flags);

        public static string Resolve(string path) {
            using (SafeFileHandle file = CreateFileW(path, 0, 7, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero)) {
                if (file.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
                StringBuilder result = new StringBuilder(32768);
                uint length = GetFinalPathNameByHandleW(file, result, (uint)result.Capacity, 0);
                if (length == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
                if (length >= result.Capacity) throw new InvalidOperationException("Path is too long.");
                string value = result.ToString();
                if (value.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase))
                    return @"\\" + value.Substring(8);
                return value.StartsWith(@"\\?\", StringComparison.Ordinal) ? value.Substring(4) : value;
            }
        }
    }
}
'@
    }
    return [IGAsciiWorkspace.NativePath]::Resolve([IO.Path]::GetFullPath($Path)).TrimEnd('\', '/')
}

function Get-IGWorkspaceKey {
    param([Parameter(Mandatory = $true)][string]$PhysicalRoot)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $bytes = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($PhysicalRoot.ToLowerInvariant())) }
    finally { $sha.Dispose() }
    return -join ($bytes[0..7] | ForEach-Object { $_.ToString('x2') })
}

function New-IGPhysicalDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $cursor = $full
    $missing = [Collections.Generic.List[string]]::new()
    while (-not (Test-Path -LiteralPath $cursor)) {
        $missing.Add((Split-Path -Leaf $cursor))
        $parent = Split-Path -Parent $cursor
        if (-not $parent -or $parent -eq $cursor) { throw "폴더를 만들 상위 경로가 없습니다: $Path" }
        $cursor = $parent
    }
    if (-not (Test-Path -LiteralPath $cursor -PathType Container)) {
        throw "상위 경로가 폴더가 아닙니다: $cursor"
    }
    $physical = Get-IGPhysicalPath $cursor
    for ($index = $missing.Count - 1; $index -ge 0; $index--) {
        $physical = Join-Path $physical $missing[$index]
    }
    # junction 아래의 새 폴더는 실제 부모 경로에 만든 뒤 별칭에서도 같은 대상을 확인한다.
    [void][IO.Directory]::CreateDirectory($physical)
    $actual = Get-IGPhysicalPath $full
    if (-not $actual.Equals($physical, [StringComparison]::OrdinalIgnoreCase)) {
        throw "새 폴더의 실제 경로가 바뀌었습니다: $full"
    }
    return $actual
}

function Assert-IGWorkspaceJunction {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$PhysicalRoot
    )
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (-not $item.PSIsContainer -or
        -not ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or $item.LinkType -ne 'Junction') {
        throw "기존 경로가 junction이 아닙니다. 덮어쓰지 않습니다: $Path"
    }
    $actualRoot = Get-IGPhysicalPath $Path
    if (-not $actualRoot.Equals($PhysicalRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "ASCII 작업 경로가 다른 폴더를 가리킵니다: $Path -> $actualRoot"
    }
    $project = Get-IGPhysicalPath (Join-Path $Path 'IndieGame.uproject')
    $expectedProject = Get-IGPhysicalPath (Join-Path $PhysicalRoot 'IndieGame.uproject')
    if (-not $project.Equals($expectedProject, [StringComparison]::OrdinalIgnoreCase)) {
        throw "ASCII 작업 경로의 프로젝트 파일이 원본과 다릅니다: $Path"
    }
    return [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
}

function Resolve-IGAsciiWorkspace {
    param(
        [Parameter(Mandatory = $true)][string]$ProjectRoot,
        [string]$LinkRoot = (Join-Path $env:LOCALAPPDATA 'IndieGame')
    )
    $physicalRoot = Get-IGPhysicalPath $ProjectRoot
    if (-not (Test-Path -LiteralPath (Join-Path $physicalRoot 'IndieGame.uproject') -PathType Leaf)) {
        throw "IndieGame.uproject가 없는 작업 폴더입니다: $physicalRoot"
    }
    if ($physicalRoot -notmatch '[^\x00-\x7F]') { return $physicalRoot }

    $base = [IO.Path]::GetFullPath($LinkRoot).TrimEnd('\', '/')
    if ($base -match '[^\x00-\x7F]') { throw "링크를 둘 경로도 ASCII여야 합니다: $base" }
    if (Test-Path -LiteralPath $base) {
        $baseItem = Get-Item -LiteralPath $base -Force
        if (-not $baseItem.PSIsContainer -or ($baseItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "링크 보관 경로는 일반 폴더여야 합니다: $base"
        }
    }
    else {
        New-IGPhysicalDirectory $base | Out-Null
    }

    $candidate = Join-Path $base 'WorkspaceLink'
    $existing = Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue
    if ($null -ne $existing) {
        # 공용 이름이 다른 저장소 또는 일반 폴더에 쓰이고 있으면 그대로 두고 별도 이름을 쓴다.
        $matching = $false
        if ($existing.PSIsContainer -and ($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -and
            $existing.LinkType -eq 'Junction') {
            try { $matching = (Get-IGPhysicalPath $candidate).Equals($physicalRoot, [StringComparison]::OrdinalIgnoreCase) }
            catch { $matching = $false }
        }
        if ($matching) { return Assert-IGWorkspaceJunction -Path $candidate -PhysicalRoot $physicalRoot }
        $candidate = Join-Path $base ('WorkspaceLink_' + (Get-IGWorkspaceKey $physicalRoot))
    }
    if ($null -eq (Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue)) {
        $physicalCandidate = Join-Path (Get-IGPhysicalPath $base) (Split-Path -Leaf $candidate)
        try { New-Item -ItemType Junction -Path $physicalCandidate -Target $physicalRoot -ErrorAction Stop | Out-Null }
        catch {
            # 동시에 같은 링크를 만든 실행이 있어도, 실제 대상이 맞는지 아래에서 다시 확인한다.
            if ($null -eq (Get-Item -LiteralPath $candidate -Force -ErrorAction SilentlyContinue)) {
                throw "ASCII junction 생성 실패: $candidate (상위 폴더: $(Test-Path -LiteralPath $base), 대상: $(Test-Path -LiteralPath $physicalRoot)). $($_.Exception.Message)"
            }
        }
    }
    return Assert-IGWorkspaceJunction -Path $candidate -PhysicalRoot $physicalRoot
}

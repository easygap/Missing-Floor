[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$readingRoot = Split-Path -Parent $PSScriptRoot
$readingImport = Join-Path $env:LOCALAPPDATA 'IndieGame/ReadingImport'
New-Item -ItemType Directory -Force -Path "$readingImport/ReadingSources", "$readingImport/Scripts", "$readingImport/Saved/Logs" | Out-Null
@'
{"FileVersion":3,"EngineAssociation":"5.8","Plugins":[{"Name":"PythonScriptPlugin","Enabled":true}]}
'@ | Set-Content -LiteralPath "$readingImport/ReadingImport.uproject" -Encoding utf8
Copy-Item -LiteralPath "$readingRoot/Content/SourceArt/AI/ShippingLabel_20260917.png" -Destination "$readingImport/ReadingSources" -Force
Copy-Item -LiteralPath "$PSScriptRoot/import_reading_art.py" -Destination "$readingImport/Scripts" -Force
$readingEditor = & "$PSScriptRoot/Resolve-UnrealEditor.ps1" -Commandlet
$readingLog = "$readingImport/Saved/Logs/ReadingImport.log"
& $readingEditor "$readingImport/ReadingImport.uproject" -unattended -nop4 -nosplash -nullrhi -nosound -RenderOffscreen `
    "-abslog=$readingLog" "-ExecutePythonScript=$readingImport/Scripts/import_reading_art.py" | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Select-String -LiteralPath $readingLog -Pattern 'READING_ART PASS')) {
    throw "문서 원본 반입 실패: $readingLog"
}
New-Item -ItemType Directory -Force -Path "$readingRoot/Content/UI/Reading" | Out-Null
Copy-Item -LiteralPath "$readingImport/Content/UI/Reading/T_ShippingLabelRead_D.uasset" -Destination "$readingRoot/Content/UI/Reading" -Force
Write-Host 'READING_ART PASS'

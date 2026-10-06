@echo off
rem visible-window: intentional (a person opens this window to play or edit)
setlocal
set "PROJECT_ROOT=%~dp0.."
set "PROJECT_FILE=%PROJECT_ROOT%\IndieGame.uproject"
set "UE_RESOLVER=%PROJECT_ROOT%\Scripts\Resolve-UnrealEditor.ps1"
set "RESOLVED_UE_EDITOR="

for /f "usebackq delims=" %%I in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%UE_RESOLVER%" -ProjectPath "%PROJECT_FILE%" 2^>nul`) do (
    set "RESOLVED_UE_EDITOR=%%I"
)

if not defined RESOLVED_UE_EDITOR (
    echo Unreal Engine matching IndieGame.uproject was not found.
    echo Install the associated engine or set IG_UNREAL_EDITOR to UnrealEditor.exe.
    pause
    exit /b 1
)

rem 없는 층 본편: 안전한 입주 저녁부터 시작해 첫날 밤과 이후 진행으로 이어진다.
start "Missing Floor" "%RESOLVED_UE_EDITOR%" "%PROJECT_FILE%" -game -windowed -ResX=1600 -ResY=900 -IGMissingFloor -IGSkipFrontend
endlocal

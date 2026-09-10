@echo off
title MU Online Client

REM MuMain 仓库内启动脚本（git 用户克隆后可直接用；需先编译出 Main.exe，且 OpenMU 在跑）
cd /d "%~dp0out\build\windows-x86\src"
if not exist Main.exe (
    echo [错误] 未找到 Main.exe，请先编译：
    echo   cmake --preset windows-x86 ^&^& cmake --build --preset windows-x86-release
    pause
    exit /b 1
)
REM 必须用完整路径：本机设了 NoDefaultCurrentDirectoryInExePath=1，cmd 不从当前目录按裸名找 exe
"%~dp0out\build\windows-x86\src\Main.exe" connect /u127.127.127.127 /p44406
pause

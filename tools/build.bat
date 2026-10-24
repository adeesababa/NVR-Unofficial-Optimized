@echo off
setlocal EnableDelayedExpansion
REM ============================================================================================
REM  NVR build: double-click, or run from any command prompt (no Visual Studio prompt needed).
REM
REM    build.bat                      builds NewVegasReloaded.dll (Release, 32-bit) into build\optimized\
REM    build.bat "D:\Games\FNV\Data"  builds, then copies the DLL (+ .pdb) to <Data>\nvse\Plugins\ and the shader
REM                                   sources to <Data>\Shaders\NewVegasReloaded\ (only changed files; nothing is deleted)
REM
REM  You can also put the Data folder path on the first line of tools\deploy-path.txt and then just double-click.
REM  Needs Visual Studio 2019 (or 2022 with the "MSVC v142 - VS 2019 C++ build tools" component installed).
REM ============================================================================================

set "REPO=%~dp0.."
for %%I in ("%REPO%") do set "REPO=%%~fI"
set "DEPLOY=%~1"
if "%DEPLOY%"=="" if exist "%~dp0deploy-path.txt" set /p DEPLOY=<"%~dp0deploy-path.txt"

echo.
echo === NVR build ===
echo Repo: %REPO%

REM --- find Visual Studio with the v142 toolset ------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo.
    echo ERROR: vswhere.exe not found. Is Visual Studio installed?
    echo        Expected: "%VSWHERE%"
    goto :fail
)
set "VS="
for /f "usebackq delims=" %%I in (`""%VSWHERE%" -latest -version "[16.0,17.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath"`) do set "VS=%%I"
if "%VS%"=="" (
    REM no VS 2019: a newer VS with the v142 toolset component also works
    for /f "usebackq delims=" %%I in (`""%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.v142.x86.x64 -property installationPath"`) do set "VS=%%I"
)
if "%VS%"=="" (
    echo.
    echo Visual Studio installs seen by vswhere:
    ""%VSWHERE%" -all -property installationPath"
    echo.
    echo ERROR: none of them has the C++ v142 toolset.
    echo        Install Visual Studio 2019 with "Desktop development with C++", or in VS 2022's installer tick
    echo        "MSVC v142 - VS 2019 C++ x64/x86 build tools".
    goto :fail
)
set "MSBUILD=%VS%\MSBuild\Current\Bin\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo.
    echo ERROR: MSBuild not found at "%MSBUILD%".
    goto :fail
)
echo Visual Studio: %VS%

REM --- submodules (imgui, toml11) must be present --------------------------------------------
if not exist "%REPO%\lib\imgui\imgui.h" (
    echo lib\imgui is empty: fetching the git submodules...
    pushd "%REPO%"
    git submodule update --init --recursive
    popd
    if not exist "%REPO%\lib\imgui\imgui.h" (
        echo.
        echo ERROR: could not fetch the submodules. Run "git submodule update --init --recursive" in %REPO% by hand.
        goto :fail
    )
)

REM --- build ------------------------------------------------------------------------------------
echo.
echo Building NewVegasReloaded.dll (Release, Win32)...
"%MSBUILD%" "%REPO%\NewVegasReloaded\NewVegasReloaded.vcxproj" /p:Configuration=Release /p:Platform=Win32 "/p:SolutionDir=%REPO%\" "/p:OutDir=%REPO%\build\optimized\" "/p:IntDir=%REPO%\build\optimized-obj\" /m /nologo /v:minimal
if errorlevel 1 (
    echo.
    echo BUILD FAILED. Scroll up for the first line marked "error".
    goto :fail
)
set "DLL=%REPO%\build\optimized\NewVegasReloaded.dll"
if not exist "%DLL%" (
    echo.
    echo ERROR: the build reported success but "%DLL%" is missing.
    goto :fail
)
echo.
echo BUILD OK: %DLL%

REM --- optional deploy ------------------------------------------------------------------------
if "%DEPLOY%"=="" (
    echo.
    echo To install: copy build\optimized\NewVegasReloaded.dll to ^<Data^>\nvse\Plugins\ and any changed .hlsl files from
    echo src\hlsl\NewVegas\Shaders\ to ^<Data^>\Shaders\NewVegasReloaded\Shaders\ ^(Effects likewise^).
    echo Or run:  build.bat "path\to\Fallout New Vegas\Data"
    goto :done
)
if not exist "%DEPLOY%\nvse\Plugins" (
    echo.
    echo ERROR: "%DEPLOY%\nvse\Plugins" does not exist. Give the game's Data folder ^(the one with nvse\Plugins in it^).
    goto :fail
)
echo.
echo Copying to %DEPLOY% ...
copy /y "%DLL%" "%DEPLOY%\nvse\Plugins\" >nul || goto :copyfail
if exist "%REPO%\build\optimized\NewVegasReloaded.pdb" copy /y "%REPO%\build\optimized\NewVegasReloaded.pdb" "%DEPLOY%\nvse\Plugins\" >nul
REM shaders: changed files only, nothing deleted (no /MIR). robocopy exit codes under 8 are success.
robocopy "%REPO%\src\hlsl\NewVegas\Shaders" "%DEPLOY%\Shaders\NewVegasReloaded\Shaders" /E /NJH /NJS /NDL /NP /NFL
if errorlevel 8 goto :copyfail
robocopy "%REPO%\src\hlsl\NewVegas\Effects" "%DEPLOY%\Shaders\NewVegasReloaded\Effects" /E /NJH /NJS /NDL /NP /NFL
if errorlevel 8 goto :copyfail
echo DEPLOYED: DLL to nvse\Plugins\, shaders to Shaders\NewVegasReloaded\.
echo If a shader change does not show in game, delete the folder Shaders\NewVegasReloaded\Shaders\Cache\ once.
goto :done

:copyfail
echo.
echo ERROR: copying to "%DEPLOY%" failed (is the game running, or the folder read-only?).
goto :fail

:fail
echo.
pause
exit /b 1

:done
echo.
pause
exit /b 0

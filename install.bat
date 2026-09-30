@echo off
rem SRTT First Person installer for Saints Row: The Third (2011 PC release, DX9 and DX11).
rem
rem   install.bat                           install into the game folder (found automatically, or asked)
rem   install.bat "<game folder>"           install into that folder
rem   install.bat /uninstall ["<folder>"]   remove the mod again
rem
rem The mod is dinput8.dll plus SRTT_FirstPerson.ini next to the game's executables; no game files are
rem changed. An existing SRTT_FirstPerson.ini is kept, so your settings survive updates (settings it
rem does not have yet use their defaults). A dinput8.dll belonging to another mod is renamed to
rem dinput8.dll.before-srtt-fp and put back on uninstall.
setlocal EnableExtensions
set "SRC=%~dp0"
set "BACKUP=dinput8.dll.before-srtt-fp"
set "RC=0"
set "MODE=install"
if "%~1"=="/?" goto usage
if /i "%~1"=="/help" goto usage
if /i not "%~1"=="/uninstall" goto find_game
set "MODE=uninstall"
shift

rem ---------------------------------------------------------------------------------------------------
rem Find the game: the folder given, the folder this MOD folder is in, the current folder, Steam.
:find_game
set "GAME="
if "%~1"=="" goto find_default
call :use_folder "%~1"
if defined GAME goto found
echo "%~1" is not the Saints Row: The Third folder (there is no SaintsRowTheThird.exe in it).
goto ask

:find_default
call :use_folder "%SRC%.."
if not defined GAME call :use_folder "%CD%"
if not defined GAME call :find_steam
if defined GAME goto found

:ask
echo.
echo Where is Saints Row: The Third installed? Paste the folder that contains SaintsRowTheThird.exe
echo (for example C:\Program Files (x86)\Steam\steamapps\common\Saints Row the Third),
set "INPUT="
set /p "INPUT=or just press Enter to cancel: "
if not defined INPUT goto cancelled
set "INPUT=%INPUT:"=%"
call :use_folder "%INPUT%"
if defined GAME goto found
echo There is no SaintsRowTheThird.exe in that folder.
goto ask

:found
tasklist /NH 2>nul | findstr /i /b /c:"SaintsRowTheThird" >nul
if errorlevel 1 goto %MODE%
echo.
echo Saints Row: The Third is running. Close the game, then run this again.
set "RC=1"
goto done

rem ---------------------------------------------------------------------------------------------------
:install
echo Installing SRTT First Person into
echo   %GAME%
if not exist "%SRC%dinput8.dll" goto missing
if not exist "%SRC%SRTT_FirstPerson.ini" goto missing
if not exist "%GAME%\dinput8.dll" goto copy_dll
call :is_ours "%GAME%\dinput8.dll"
if not errorlevel 1 goto copy_dll

echo.
echo The game folder already has a dinput8.dll from another mod (an ASI loader, for example).
echo Only one can be used at a time. The other one will be renamed to %BACKUP%
echo and put back if you uninstall this mod.
if exist "%GAME%\%BACKUP%" goto backup_exists
set "ANSWER="
set /p "ANSWER=Continue? [y/N] "
if /i not "%ANSWER%"=="y" goto cancelled
move /y "%GAME%\dinput8.dll" "%GAME%\%BACKUP%" >nul
if errorlevel 1 goto copy_failed
echo Renamed the other mod's dinput8.dll to %BACKUP%.

:copy_dll
copy /y "%SRC%dinput8.dll" "%GAME%\dinput8.dll" >nul
if errorlevel 1 goto copy_failed
if exist "%GAME%\SRTT_FirstPerson.ini" goto keep_ini
copy /y "%SRC%SRTT_FirstPerson.ini" "%GAME%\SRTT_FirstPerson.ini" >nul
if errorlevel 1 goto copy_failed
echo Copied dinput8.dll and SRTT_FirstPerson.ini.
goto installed
:keep_ini
echo Copied dinput8.dll and kept your existing SRTT_FirstPerson.ini.
:installed
echo.
echo Done. Start the game and press L to switch between first and third person.
echo Settings are in SRTT_FirstPerson.ini in the game folder (see README.md).
goto done

rem ---------------------------------------------------------------------------------------------------
:uninstall
echo Removing SRTT First Person from
echo   %GAME%
if not exist "%GAME%\dinput8.dll" goto not_installed
call :is_ours "%GAME%\dinput8.dll"
if errorlevel 1 goto not_ours
del /f /q "%GAME%\dinput8.dll" 2>nul
if exist "%GAME%\dinput8.dll" goto delete_failed
echo Removed dinput8.dll.
if not exist "%GAME%\%BACKUP%" goto remove_extras
move /y "%GAME%\%BACKUP%" "%GAME%\dinput8.dll" >nul
if not errorlevel 1 echo Put back the other mod's dinput8.dll.
:remove_extras
if exist "%GAME%\SRTT_FirstPerson.log" del /f /q "%GAME%\SRTT_FirstPerson.log" 2>nul
if not exist "%GAME%\SRTT_FirstPerson.ini" goto uninstalled
set "ANSWER="
set /p "ANSWER=Also delete your settings (SRTT_FirstPerson.ini)? [y/N] "
if /i not "%ANSWER%"=="y" goto uninstalled
del /f /q "%GAME%\SRTT_FirstPerson.ini" 2>nul
echo Deleted SRTT_FirstPerson.ini.
:uninstalled
echo.
echo Done. The game is back to normal.
goto done

rem ---------------------------------------------------------------------------------------------------
:usage
echo Installs the SRTT First Person mod into Saints Row: The Third.
echo.
echo   install.bat                           install (the game folder is found automatically or asked for)
echo   install.bat "game folder"             install into that folder
echo   install.bat /uninstall ["game folder"]  remove the mod
goto done

:missing
echo.
echo The mod files (dinput8.dll, SRTT_FirstPerson.ini) are missing from
echo   %SRC%
set "RC=1"
goto done

:backup_exists
echo.
echo %BACKUP% already exists in the game folder. Move it somewhere else first.
set "RC=1"
goto done

:copy_failed
echo.
echo Could not write to the game folder. If the game is installed under Program Files, right-click
echo install.bat and choose "Run as administrator".
set "RC=1"
goto done

:delete_failed
echo.
echo Could not delete dinput8.dll. If the game is installed under Program Files, right-click
echo install.bat and choose "Run as administrator".
set "RC=1"
goto done

:not_installed
echo The mod is not installed there (no dinput8.dll).
goto done

:not_ours
echo The dinput8.dll there is not this mod's, so it was left alone.
set "RC=1"
goto done

:cancelled
echo Cancelled, nothing was changed.
set "RC=1"
goto done

:done
rem started by double-clicking: keep the window open to show the result
echo(%cmdcmdline%| find /i " /c " >nul && (echo. & pause)
endlocal & exit /b %RC%

rem ---------------------------------------------------------------------------------------------------
rem Sets GAME to the full path of %1 if it is the game folder.
:use_folder
if exist "%~1\SaintsRowTheThird.exe" set "GAME=%~f1"
if exist "%~1\SaintsRowTheThird_DX11.exe" set "GAME=%~f1"
exit /b 0

rem Errorlevel 0 if %1 is this mod's dinput8.dll.
:is_ours
findstr /m /c:"SRTT First Person" "%~1" >nul 2>&1
exit /b %errorlevel%

rem Looks for the game in the Steam folder and every Steam library folder.
:find_steam
set "STEAM="
for /f "tokens=2,*" %%a in ('reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| find /i "SteamPath"') do set "STEAM=%%b"
if not defined STEAM exit /b 0
set "STEAM=%STEAM:/=\%"
call :use_folder "%STEAM%\steamapps\common\Saints Row the Third"
if defined GAME exit /b 0
if not exist "%STEAM%\steamapps\libraryfolders.vdf" exit /b 0
for /f "usebackq tokens=1,*" %%a in ("%STEAM%\steamapps\libraryfolders.vdf") do if /i "%%~a"=="path" call :steam_library "%%~b"
exit /b 0

:steam_library
if defined GAME exit /b 0
set "LIB=%~1"
set "LIB=%LIB:\\=\%"
call :use_folder "%LIB%\steamapps\common\Saints Row the Third"
exit /b 0

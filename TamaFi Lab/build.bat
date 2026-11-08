@echo off
cd /d "%~dp0"
py -3 -m pip install -r requirements.txt pyinstaller -q
py -3 -m PyInstaller --noconfirm --clean --windowed --name TamaFiLab --icon "ui\brand\app.ico" --add-data "ui;ui" --collect-all webview --collect-all serial --hidden-import serial --hidden-import serial.serialwin32 --hidden-import serial.win32 --hidden-import serial.tools.list_ports --hidden-import serial.tools.list_ports_windows --hidden-import serial.tools.list_ports_common --hidden-import esptool --hidden-import webview --hidden-import webview.platforms.edgechromium --hidden-import clr --hidden-import pythonnet tamafi_lab.py
if errorlevel 1 (
  echo Build failed.
  pause
  exit /b 1
)

rem The file in build\ is an incomplete stub. Never run it.
if exist "build\TamaFiLab\TamaFiLab.exe" del /q "build\TamaFiLab\TamaFiLab.exe"

powershell -NoProfile -Command ^
  "$ws = New-Object -ComObject WScript.Shell;" ^
  "$lnk = $ws.CreateShortcut([IO.Path]::GetFullPath('TamaFi Lab.lnk'));" ^
  "$lnk.TargetPath = [IO.Path]::GetFullPath('dist\TamaFiLab\TamaFiLab.exe');" ^
  "$lnk.WorkingDirectory = [IO.Path]::GetFullPath('dist\TamaFiLab');" ^
  "$ico = [IO.Path]::GetFullPath('ui\brand\app.ico');" ^
  "if (Test-Path $ico) { $lnk.IconLocation = $ico };" ^
  "$lnk.Save()"

echo.
echo Run this file:
echo   %cd%\dist\TamaFiLab\TamaFiLab.exe
echo Or the shortcut:
echo   %cd%\TamaFi Lab.lnk
echo.
echo Do not open anything inside the build\ folder.
start "" explorer /select,"%cd%\dist\TamaFiLab\TamaFiLab.exe"
pause

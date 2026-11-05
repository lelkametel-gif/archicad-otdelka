@echo off
rem Собирает HimeraZone_АСП.gsm из HimeraZone_АСП.xml конвертером Archicad 27 (файлы в одной папке).
chcp 65001 >nul
set CONV=
for %%P in ("%ProgramFiles%\GRAPHISOFT\Archicad 27" "%ProgramFiles%\GRAPHISOFT\ARCHICAD 27") do (
  if exist "%%~P\LP_XMLConverter.exe" set CONV=%%~P\LP_XMLConverter.exe
)
if "%CONV%"=="" (
  echo Не найден LP_XMLConverter.exe. Укажите путь к папке Archicad 27 вручную в этом файле.
  pause
  exit /b 1
)
"%CONV%" xml2libpart "%~dp0HimeraZone_АСП.xml" "%~dp0HimeraZone_АСП.gsm"
if errorlevel 1 (echo ОШИБКА сборки — пришлите текст выше.) else (echo Готово: HimeraZone_АСП.gsm)
pause

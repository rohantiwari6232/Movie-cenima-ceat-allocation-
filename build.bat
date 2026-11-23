@echo off
REM CineSeat build script (Windows, MinGW g++). Run: build.bat
if not exist sqlite3.c (
  echo Downloading SQLite source...
  powershell -NoProfile -Command "Invoke-WebRequest https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip -OutFile sq.zip; Expand-Archive sq.zip -DestinationPath sq -Force; Copy-Item sq\*\sqlite3.c,sq\*\sqlite3.h -Destination ."
  if exist sq rmdir /s /q sq
  if exist sq.zip del sq.zip
)
if not exist sqlite3.c (
  echo.
  echo AUTO DOWNLOAD FAIL. Manually sqlite.org/download.html se "sqlite-amalgamation" zip download karo
  echo aur usme se sqlite3.c aur sqlite3.h is folder me copy karke build.bat dobara chalao.
  exit /b 1
)
if not exist sqlite3.o gcc -c sqlite3.c -o sqlite3.o
g++ -std=c++17 main.cpp sqlite3.o -o cineseat.exe && echo Build OK

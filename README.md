# CineSeat - VS Code me run karne ke steps (Windows)

Pehle check: terminal me `g++ --version` chalao. Version aaye to MinGW installed hai.
(Nahi aaya to MSYS2/MinGW-w64 install karke uski bin folder PATH me add karo.)

1. Zip extract karo, VS Code me File > Open Folder > `CineSeat`
2. Terminal kholo (Ctrl + `) aur chalao:

       build.bat

   (pehli baar SQLite khud download hoga aur compile hoga, 1 min lag sakta hai)
   Ya Ctrl+Shift+B dabao (same kaam karta hai).
3. Run:

       .\cineseat.exe

   Agar PowerShell me error aaye to: `cmd /c cineseat.exe`

Database file `cineseat.db` isi folder me apne aap ban jati hai.
Reset karna ho to `cineseat.db` delete kar do.

## Linux / Mac
    sh build.sh && ./cineseat

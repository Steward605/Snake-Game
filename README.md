# Snake Game

Old assignment Snake game written in C for Windows. The game uses GTK4 for the menu/dialog windows, SDL2 for rendering, SDL2_image/SDL2_ttf for assets and text, and SQLite to save best score.

This project uses MSYS2 MinGW64. The `scripts/package-windows.ps1` script creates a runnable folder at `dist/snake_game`.

## Install build dependencies

Install MSYS2, then open the **MSYS2 MSYS** terminal and update it:

```sh
pacman -Syu
```

Install the MinGW64 packages:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf mingw-w64-x86_64-gtk4 mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_image mingw-w64-x86_64-SDL2_ttf mingw-w64-x86_64-sqlite3
```

The scripts assume MSYS2 is installed at `C:\msys64`.

## Build

From PowerShell in this folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

The rebuilt executable is written to:

```text
build\snake_game.exe
```

## Create a runnable release folder

From PowerShell in this folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package-windows.ps1
```

Run the packaged game with:

```text
dist\snake_game\run-snake-game.bat
```

The release folder `dist\snake_game` contains the executable, `media`, and the MinGW runtime DLLs needed by the game.

```powershell
Compress-Archive -Path .\dist\snake_game\* -DestinationPath .\dist\snake_game-windows-x64.zip -Force
```

## Troubleshooting

If you see an error like `libgtk-4-1.dll was not found`, GTK4 is not installed or the game is being run without the packaged DLLs. Install the packages above, then run `scripts/package-windows.ps1`.

If you see `Entry Point Not Found` errors such as `clock_gettime64`, MSYS2 has a mixed old/new package set. Run `pacman -Syu`, close/reopen MSYS2 if requested, then run `pacman -Syu` again until there are no pending core updates. After that, reinstall the dependency command above.
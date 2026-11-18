# Luce CMD source integration

The `reactos/` interpreter and `conutils/` stream helpers are adapted from the
corresponding local ReactOS sources. They retain the upstream copyright and
GPL-2.0-or-later notices; each migrated translation unit records the Luce
C++23 port date. See the included `reactos/license.txt` and `conutils/LICENSE`.

The build compiles these sources as C++23 and links them into `cmd.exe`. Runtime
imports are resolved only from Luce's own `kernel32`, `advapi32`, `shell32`,
`msvcrt`, `ucrtbase`, `ntdll`, `user32`, `gdi32`, and `win32k` DLLs. No Wine
library, ReactOS DLL, or third-party DLL is a CMD dependency.

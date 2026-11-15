# Need for Speed Most Wanted (2005, PC) — Recompiled


### Android downloads / Descargas Android

[Most Wanted v0.1.7](https://github.com/codepdbh/nfsmwpc-recompiled/releases/tag/v0.1.7)

| APK | Architecture |
|---|---|
| [ARM64 / 64 bits](https://github.com/codepdbh/nfsmwpc-recompiled/releases/download/v0.1.7/NFSMWPC-AndroidEvolved-v0.1.7-arm64.apk) | arm64-v8a, recommended |
| [ARMv7 / 32 bits](https://github.com/codepdbh/nfsmwpc-recompiled/releases/download/v0.1.7/NFSMWPC-AndroidEvolved-v0.1.7-armeabi-v7a.apk) | armeabi-v7a; requires Android with 32-bit app support |

Android 10+ and Vulkan 1.1+. Both APKs use the same package and signing key;
install the architecture supported by your device. Original game data is required.
Audio, pacing and validation details: [runtime follow-up](docs/AUDIO_FRAME_PACING.md).

**English** · [Español](#español)

Static recompilation of the **PC version** of *Need for Speed: Most Wanted* (2005, `speed.exe`, x86) to native code, first as a Windows validation host and then for **Android ARM64 and ARMv7**. The goal is the third game of the *NFS Android Evolved* family, after [Underground 2](https://github.com/codepdbh/nfsu2-recompiled) and [Underground 1](https://github.com/codepdbh/nfsu1-recompiled).

This is **not** an emulator and **not** a port of the Xbox 360 version: every function of the original PC executable is translated ahead of time from x86 to C with [pcrecomp](https://github.com/sp00nznet/pcrecomp) (patched), then compiled by MSVC (Windows) or the Android NDK (ARM64 / ARMv7).

> No game files are included. You need your own copy of the PC game. Generated C code, analysis copies and logs are produced locally and are never committed.

### Status

| Milestone | State |
|---|---|
| MW-M0 Executable analysis (`speed.exe`, SHA-256 `80774c2e…253c`) | ✅ Direct3D 9 + D3DX effects, DirectInput 8, DirectSound, relocations recovered |
| MW-M1 Function catalog | ✅ 41,262 functions (data, switch-table, immediate and forced seeds) |
| MW-M2 C generation | ✅ 0 lift failures |
| MW-M3 Windows host build and link | ✅ |
| MW-M4/M5 CRT, memory, original entry point | ✅ |
| MW-M6/M7 Win32, Direct3D 9, first frame | ✅ D3DX effects loaded from the game's own resources |
| MW-M8/M9 Intro, menus, world | ✅ Cinematics, front end, career map and free roam; touch cursor, keyboard and gamepad |
| MW-M10+ Racing, police, saving | ✅ User-confirmed fully playable; LAN and Online (EA Nation replacement server) played on two phones |
| Android ARM64 | ✅ Playable (S25 Ultra, Redmi Note 8 and tester phones); reports are tracked in [issues](https://github.com/codepdbh/nfsmwpc-recompiled/issues) |
| Android ARMv7 | Builds; not yet tested on a device |

### Playing on Android

1. Install the APK for your architecture (arm64 recommended).
2. Copy your PC installation into `Internal storage/nfsmw` so that `nfsmw/Speed.exe` exists
   next to `GLOBAL`, `TRACKS`, `CARS`, `FRONTEND`, `LANGUAGES`, `SOUND`, `MOVIES` and `NIS`.
   Add `d3dx9_26.dll` (DirectX runtime, from your Windows `SysWOW64`) and, for LAN hosting,
   `server.dll` and `server.cfg` from the game.
3. Open the launcher, pick your language with the flags (Español, English, Português),
   grant file access and press **PLAY**.

The launcher sets the internal resolution, FPS limit, proportional widescreen HUD, minimap position
(top-left by default, so the steering thumb does not cover it), CPU/GPU latency, compressed texture
mode, Vulkan driver (system or a custom Turnip ZIP) and the Online server. In game, the on-screen
controls can be moved, resized and switched to classic, Xbox or PlayStation layouts, or replaced by
tilt steering or a Bluetooth/USB gamepad.

### What's new in v0.1.7

- The game's video options are kept between launches (they were reset on every start); render resolution and antialiasing stay under the launcher's control.
- After an incompatible driver probe the launcher offers "Play anyway".

### What's new in v0.1.6

- Touch buttons for the world map (M) and text messages (Tab) while driving.
- Touch controls hide again when you keep playing with a Bluetooth/USB gamepad after touching the screen.
- Guest threads hand over the machine less often (the Underground 2 change that raised FPS ~30% on a Redmi Note 8).
- Audio pauses with the game in the background instead of repeating the last sound.
- Addresses the startup crash in a localized-string copy on several MediaTek phones (heap slack after blocks).
- Android 10: the game folder can be read again (legacy storage).

### What's new in v0.1.5

- Minimap moved to the top-left corner in races (launcher option).
- Bluetooth/USB gamepads: only external devices count as pads (also those in keyboard mode), and
  by default they drive the game's keyboard keys; the analog DirectInput joystick is an option.
- Car steering left by itself on some MediaTek phones: input from built-in sensors exposed as
  joysticks is ignored, and touch buttons whose release event was lost are let go.
- DXVK crash in `D3D9Surface::ClearContainer` after a failed device Reset (issues #2, #3, #13).
- Fewer hitches: the Vulkan pipeline cache is kept between sessions, DXT textures decoded on the
  CPU only decode the locked region, dynamic vertex/index buffers no longer read back GPU memory
  on every lock, and per-frame log lines were removed.
- Launcher in Spanish, English or Portuguese; clearer messages when the game files are missing or
  were copied one folder too deep; Danish, Finnish and Polish game languages.

Android runtime fixes and current verification limits: [bring-up notes](docs/ANDROID_BRINGUP.md).

### Layout

```
config/    reference executable, forced seeds, hooks, overrides (Most Wanted only)
scripts/   binary analysis, seed discovery, lift driver, Windows build/test
src/       Windows host, runtime adapters, Most Wanted shims
docs/      binary report, Android bring-up notes, mod-loader design and safety contract
android/   Android app (launcher, touch controls, D3D9→Vulkan bridge, guest runtime)
work/      (local only) analysis copy, catalog, logs
```

Shared infrastructure (Win32 shims, D3D9 observers, logging) is reused from the Underground 2 project at build time without modifying it.

### Building (Windows)

Requirements: Visual Studio with MSVC x86, Python 3 with `capstone` and `pefile`, the patched `pcrecomp` and `nfsu2-recompiled` checked out next to this folder, and the game in `../Need for Speed Most Wanted 2005`.

```powershell
python scripts/analyze_binary.py            # writes work/speed.analysis.exe and work/binary.json
python scripts/data_seeds.py ...            # see docs/BINARY_REPORT.md for the full sequence
python scripts/run_lift.py catalog work/speed.analysis.exe work/seeds_code.json work/catalog.json
python scripts/run_lift.py lift work/speed.analysis.exe work/catalog.json src/recomp/gen
powershell -File scripts/build_windows.ps1
powershell -File scripts/test_windows.ps1   # windowed test run
```

### Android LAN server module

Before building the Android APK, prepare the LAN server from your own PC copy:

```powershell
python scripts/prepare_server.py "../Need for Speed Most Wanted 2005/server.dll"
```

Keep `server.dll` and `server.cfg` from that copy in the phone's `nfsmw` folder.
The original files and generated translations are excluded from Git. Android
builds require the generated server module so LAN hosting cannot be silently
omitted. LAN and Online races reached gameplay on an S25 Ultra and a Redmi
Note 8 using ARM64 APKs. Online was confirmed with the Redmi hosting and
a VPN active on the S25; other network configurations and chat delivery
remain under validation. The ARMv7 build has not yet been tested on a device.

### License and credits

Code in this repository: see each file. pcrecomp by sp00nznet. Proportional HUD adaptation follows ThirteenAG's WidescreenFix ([notice](THIRD_PARTY_NOTICES.md)). *Need for Speed* is a trademark of Electronic Arts; this project is not affiliated with EA.

---

## Español

Recompilación estática de la **versión de PC** de *Need for Speed: Most Wanted* (2005, `speed.exe`, x86) a código nativo: primero un host de validación en Windows y después **Android ARM64 y ARMv7**. Es el tercer juego de la familia *NFS Android Evolved*, después de [Underground 2](https://github.com/codepdbh/nfsu2-recompiled) y [Underground 1](https://github.com/codepdbh/nfsu1-recompiled).

**No** es un emulador ni un port de la versión de Xbox 360: cada función del ejecutable original de PC se traduce de antemano de x86 a C con [pcrecomp](https://github.com/sp00nznet/pcrecomp) (modificado) y después se compila con MSVC (Windows) o con el NDK de Android (ARM64 / ARMv7).

> No se incluyen archivos del juego. Necesitas tu propia copia del juego de PC. El código C generado, las copias de análisis y los registros se producen localmente y nunca se suben.

### Estado

| Hito | Estado |
|---|---|
| MW-M0 Análisis del ejecutable (`speed.exe`, SHA-256 `80774c2e…253c`) | ✅ Direct3D 9 + efectos D3DX, DirectInput 8, DirectSound, reubicaciones recuperadas |
| MW-M1 Catálogo de funciones | ✅ 41.262 funciones (semillas de datos, tablas switch, inmediatos y forzadas) |
| MW-M2 Generación de C | ✅ 0 fallos de lifting |
| MW-M3 Compilación y enlace del host de Windows | ✅ |
| MW-M4/M5 CRT, memoria, punto de entrada original | ✅ |
| MW-M6/M7 Win32, Direct3D 9, primer frame | ✅ efectos D3DX cargados desde los recursos del propio juego |
| MW-M8/M9 Intro, menús, mundo | ✅ cinemáticas, menús, mapa de carrera y modo libre; cursor táctil, teclado y mando |
| MW-M10+ Carreras, policía, guardado | ✅ totalmente jugable según las pruebas del autor; LAN y Online (servidor sustituto de EA Nation) jugados en dos teléfonos |
| Android ARM64 | ✅ jugable (S25 Ultra, Redmi Note 8 y teléfonos de testers); los reportes se siguen en las [issues](https://github.com/codepdbh/nfsmwpc-recompiled/issues) |
| Android ARMv7 | Compila; aún sin probar en un dispositivo |

### Cómo jugar en Android

1. Instala el APK de tu arquitectura (se recomienda arm64).
2. Copia tu instalación de PC en `Memoria interna/nfsmw`, de forma que exista `nfsmw/Speed.exe`
   junto a `GLOBAL`, `TRACKS`, `CARS`, `FRONTEND`, `LANGUAGES`, `SOUND`, `MOVIES` y `NIS`.
   Añade `d3dx9_26.dll` (runtime de DirectX, de la carpeta `SysWOW64` de tu Windows) y, para
   crear partidas LAN, `server.dll` y `server.cfg` del juego.
3. Abre el launcher, elige tu idioma con las banderas (Español, English, Português), da acceso
   a los archivos y pulsa **JUGAR**.

El launcher permite elegir resolución interna, límite de FPS, HUD panorámico proporcional, posición
del minimapa (arriba a la izquierda por defecto, para que el pulgar del volante no lo tape), latencia
CPU/GPU, modo de texturas comprimidas, driver Vulkan (del sistema o un ZIP de Turnip) y servidor
Online. En el juego, los controles táctiles se pueden mover, redimensionar y cambiar a los estilos
clásico, Xbox o PlayStation, o sustituir por inclinación o por un mando Bluetooth/USB.

### Novedades de v0.1.7

- Las opciones de vídeo del juego se conservan entre partidas (antes volvían a las predeterminadas en cada arranque); la resolución y el antialiasing siguen controlados por el launcher.
- Si la prueba del driver dice que faltan funciones, el launcher ofrece «Jugar igualmente».

### Novedades de v0.1.6

- Botones táctiles de mapa (M) y mensajes (Tab) durante la conducción.
- Los controles táctiles se vuelven a ocultar si sigues jugando con un mando Bluetooth/USB después de tocar la pantalla.
- Los hilos del juego se turnan menos veces (el cambio de Underground 2 que subió ~30 % los FPS en un Redmi Note 8).
- El audio se pausa con el juego en segundo plano en lugar de repetir el último sonido.
- Aborda el cierre al arrancar en una copia de texto traducido en varios MediaTek (margen de memoria tras cada bloque).
- Android 10: se vuelve a poder leer la carpeta del juego (almacenamiento heredado).

### Novedades de v0.1.5

- Minimapa en la esquina superior izquierda durante las carreras (opción del launcher).
- Mandos Bluetooth/USB: solo cuentan los dispositivos externos (también los que están en modo
  teclado) y, por defecto, manejan las teclas del juego; el joystick DirectInput analógico es opcional.
- El auto giraba solo a la izquierda en algunos MediaTek: se ignoran los sensores del teléfono que
  se presentan como joystick y se sueltan los botones táctiles cuyo evento de soltar se perdió.
- Cierre de DXVK en `D3D9Surface::ClearContainer` tras un Reset fallido (issues #2, #3, #13).
- Menos tirones: la caché de pipelines de Vulkan se conserva entre sesiones, las texturas DXT que
  se convierten en la CPU solo decodifican la zona bloqueada, los buffers dinámicos ya no releen la
  memoria de la GPU en cada bloqueo y se quitaron registros que se escribían en cada frame.
- Launcher en español, inglés o portugués; mensajes más claros cuando faltan los archivos o se
  copiaron una carpeta de más; idiomas del juego danés, finés y polaco.

### Compilación (Windows)

Requisitos: Visual Studio con MSVC x86, Python 3 con `capstone` y `pefile`, `pcrecomp` (modificado) y `nfsu2-recompiled` junto a esta carpeta, y el juego en `../Need for Speed Most Wanted 2005`. Los pasos están en la sección en inglés.

### Licencia y créditos

Código de este repositorio: ver cada archivo. pcrecomp por sp00nznet. La adaptación del HUD proporcional sigue el WidescreenFix de ThirteenAG ([aviso](THIRD_PARTY_NOTICES.md)). *Need for Speed* es una marca de Electronic Arts; este proyecto no está afiliado a EA.

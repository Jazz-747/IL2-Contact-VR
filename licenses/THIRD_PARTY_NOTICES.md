# Third-party components / Сторонние компоненты

IL-2 Contact VR is distributed under the IL-2 Contact VR Freeware License (see `LICENSE`). It includes the following third-party components, each under its own license. The full license texts and copyright notices of the components that are included in the program are in the `doc\licenses` folder next to the program (file `THIRD_PARTY_LICENSES.txt`).

| Component | Used for | License |
|---|---|---|
| Python | interpreter, bundled into the program | PSF License |
| Tcl/Tk (via tkinter) | window system, bundled | Tcl/Tk license (BSD-style) |
| customtkinter | program window (includes the Roboto font, Apache-2.0) | MIT |
| darkdetect | system theme detection (pulled in by customtkinter) | BSD-3-Clause |
| packaging | helper library (pulled in by customtkinter) | Apache-2.0 or BSD-2-Clause |
| Pillow (PIL) | images in the window (pulled in by customtkinter) | HPND (MIT-CMU) |
| pygame (with SDL) | reading a joystick button for centering; bundled as separate library files, so it can be replaced | LGPL-2.1 |
| PyInstaller | building the program (build tool; its bootloader is included) | GPL-2.0-or-later with a special exception that allows distributing the built program under any license |
| OpenXR SDK headers (Khronos) | building the OpenXR layer (compile-time only) | Apache-2.0 |

Windows APIs (SendInput, registry, UAC, winsound, Direct3D 11) are provided by the operating system.

The list is produced from the build environment by `SYS/collect_licenses.py`; regenerate it before each release.

---

Программа IL-2 Contact VR распространяется по лицензии IL-2 Contact VR Freeware License (см. `LICENSE`). В неё входят перечисленные выше сторонние компоненты, каждый на условиях своей лицензии. Полные тексты лицензий и уведомления об авторских правах компонентов, вошедших в программу, лежат в папке `doc\licenses` рядом с программой (файл `THIRD_PARTY_LICENSES.txt`).

pygame (с SDL) входит в программу отдельными файлами библиотек, чтобы пользователь мог заменить их своей версией (требование LGPL).

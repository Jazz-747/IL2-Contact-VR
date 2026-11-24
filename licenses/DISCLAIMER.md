English | [Русский](#русский)

# Terms of use and disclaimer

IL-2 Contact VR is free software distributed under the [IL-2 Contact VR Freeware License](LICENSE) (Russian translation: [LICENSE.ru.md](LICENSE.ru.md)). In addition to the license, please note:

**Unofficial project.** The program is not affiliated with, endorsed by or supported by 1C Entertainment, 777 Studios, Meta, Pico, Valve, Khronos or any other company. IL-2 Sturmovik, IL-2 Great Battles, Meta Quest, Pico, OpenXR, Windows and other names are trademarks of their respective owners and are used only to describe compatibility.

**The game is not modified.** The program does not read or change the game's files or memory, except for the `data/startup.cfg` telemetry settings, which it edits only on your request and after making a backup copy. It sends the game ordinary keyboard and mouse input, and receives the game's public telemetry.

**Multiplayer.** The program simulates keyboard and mouse input. Rules of multiplayer servers differ: before using it online, make sure it is allowed on that server. The author is not responsible for any restrictions applied to your account or on a server.

**OpenXR layer and changes to your system.** The layer (`XR_APILAYER_IL2CONTACT`) is registered as an implicit OpenXR layer: the program writes an entry to the Windows registry (HKLM, administrator rights are requested by Windows) and keeps a working copy of the layer files in `%ProgramData%\IL2Contact`. The layer is loaded into every OpenXR application, not only IL-2. It can be switched off with the environment variable `DISABLE_XR_APILAYER_IL2CONTACT=1` or removed from the program settings, which deletes the registry entry. The layer files are not digitally signed, so Windows or your antivirus may show a warning. Install the program only from the official releases page.

**Health and safety.** Use VR in a safe place, take breaks, and do not use the program if VR causes you discomfort. The author is not responsible for injury, damage to property or equipment.

**No warranty.** The program is provided "as is", without any warranty. Hand tracking and zone positions are approximate and may fail; do not rely on the program for anything where a failure could cause harm.

**Data.** The program does not collect or send personal data. Settings, profiles, calibration data and the program log are stored locally next to the program; the layer's working files and log are stored in `%ProgramData%\IL2Contact` (or, if it is not writable, `%LOCALAPPDATA%\IL2Contact`). The only network request is an optional check for a new version on GitHub; it can be switched off in the settings.

**Donations.** Donations are voluntary, are a gift and not a payment for the program or for any service, and do not give any additional rights or a right to support.

---

## Русский

# Условия использования и отказ от ответственности

IL-2 Contact VR это бесплатная программа, распространяемая по [лицензии IL-2 Contact VR Freeware License](LICENSE) (русский перевод: [LICENSE.ru.md](LICENSE.ru.md)). Помимо лицензии, обратите внимание:

**Неофициальный проект.** Программа никак не связана с компаниями 1C Entertainment, 777 Studios, Meta, Pico, Valve, Khronos и другими, не одобрена и не поддерживается ими. IL-2 Sturmovik, IL-2 Great Battles, Meta Quest, Pico, OpenXR, Windows и другие названия являются товарными знаками их владельцев и используются только для описания совместимости.

**Игра не изменяется.** Программа не читает и не меняет файлы и память игры, кроме настроек телеметрии в `data/startup.cfg`: она правит их только по вашей просьбе и после создания резервной копии. В игру отправляются обычные нажатия клавиатуры и мыши, из игры принимается её штатная телеметрия.

**Многопользовательская игра.** Программа имитирует нажатия клавиатуры и мыши. Правила серверов различаются: перед использованием онлайн убедитесь, что это разрешено на выбранном сервере. Автор не отвечает за ограничения, наложенные на ваш аккаунт или сервер.

**Слой OpenXR и изменения в системе.** Слой (`XR_APILAYER_IL2CONTACT`) регистрируется как неявный слой OpenXR: программа записывает запись в реестр Windows (HKLM, права администратора запрашивает Windows) и хранит рабочую копию файлов слоя в `%ProgramData%\IL2Contact`. Слой подгружается во все OpenXR-приложения, а не только в IL-2. Его можно отключить переменной окружения `DISABLE_XR_APILAYER_IL2CONTACT=1` или удалить в настройках программы (запись в реестре при этом удаляется). Файлы слоя не имеют цифровой подписи, поэтому Windows или антивирус могут показать предупреждение. Устанавливайте программу только со страницы официальных релизов.

**Здоровье и безопасность.** Используйте VR в безопасном месте, делайте перерывы и не пользуйтесь программой, если VR вызывает дискомфорт. Автор не отвечает за травмы, порчу имущества и оборудования.

**Без гарантий.** Программа предоставляется «как есть», без каких-либо гарантий. Слежение за руками и положение зон приблизительны и могут отказать; не полагайтесь на программу там, где сбой может причинить вред.

**Данные.** Программа не собирает и не отправляет персональные данные. Настройки, профили, калибровка и журнал программы хранятся локально рядом с программой; рабочие файлы слоя и его журнал хранятся в `%ProgramData%\IL2Contact` (если туда писать нельзя, то в `%LOCALAPPDATA%\IL2Contact`). Единственный сетевой запрос это необязательная проверка новой версии на GitHub, её можно отключить в настройках.

**Пожертвования.** Пожертвования добровольны, являются подарком, а не оплатой программы или услуги, и не дают дополнительных прав и права на поддержку.

# Changelog

🇺🇸 English · 🇧🇷 [Português](#-português)

All notable changes to Juice. Versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- **Galaxy Buds: each earbud and the case** (Buds+ and later), read from the earbuds' own channel: one row with
  the earbud that runs out first, each earbud and the case in the status (optional, off by default; the channel
  is opened at most every 10 minutes). Tested with the Galaxy Buds2.

## [1.2.0] — 2026-11-25

### Added
- **PlayStation controllers** over Bluetooth: DualSense, DualSense Edge and DualShock 4 *(not tested yet)*.
- **AirPods and Beats** from their Bluetooth LE advertisements: one row with the earbud that runs out first,
  each earbud and the case in the status (optional, off by default; *not tested yet*).
- **Time left estimate** ("~2 days left") from how fast the level drops since the last charge.
- **Full charge notification** when a charging device reaches 100% (on by default).
- **One tray icon per device** (optional).
- **Portable mode**: a `juice.ini` next to `Juice.exe` is used instead of `%APPDATA%\Juice`.
- **Program icon** (the juice box) and version info in `Juice.exe`.
- Release builds by **GitHub Actions**, with `Juice.exe.sha256`; **winget** manifests.

### Changed
- **Settings** reorganized in sections (Devices, Icons, Refresh and warnings, General), following the Windows
  light/dark theme; the device list only has the names.
- JBL speakers are listened for ~5 s on each refresh (was ~2.5 s, which sometimes missed the speaker).

## [1.1.0] — 2026-11-24

### Added
- **JBL speakers** over Bluetooth LE (optional, off by default: *Settings → Read the battery of JBL speakers*).
  Tested with the JBL Flip 7.
- **Xbox Wireless Controller** over Bluetooth, from the battery report the controller sends by itself
  (4 steps: 10/40/70/100%, charging when on USB power).
- **Last known level** of disconnected devices, greyed out with how long ago it was read, kept across
  restarts (*Settings → Show the last level of disconnected devices*, on by default).
- **Remove a device** from the list: hover over a disconnected device and click **×**.
- New **speaker** and **controller** illustrations and tray icons.
- `probe.exe` also lists JBL speakers, Xbox controllers and the serial number of Logitech devices.

### Changed
- Each device's type is remembered, so an offline device keeps its icon (e.g. a headset that is turned off).
- Logitech devices are identified by their serial number: a mouse used on its dongle and on Bluetooth
  (like the G309) is a single row, and its Windows Bluetooth battery entry is merged into it.
- A paired device that is not connected only shows up after it connects at least once.

### Fixed
- Two Logitech receivers of the same model (e.g. two LIGHTSPEED dongles) shared the same device key, so
  one mouse hid the other.
- The name and type of a device that answers but has no battery reading (a headset turned off) are now
  kept.

### Tested
- Galaxy Buds2 and Galaxy Buds3 Pro (Bluetooth), G309 (LIGHTSPEED and Bluetooth), JBL Flip 7, Xbox One S
  controller.

## [1.0.0] — 2026-11-24

First release: battery of Logitech HID++ devices and Bluetooth headsets on the taskbar button (progress bar
and badge), compact window, tray icon when closed, low battery notification, lowest / fixed / rotating
device, English and Portuguese.

---

## 🇧🇷 Português

Todas as mudanças relevantes do Juice. As versões seguem o [Versionamento Semântico](https://semver.org/lang/pt-BR/).

## [Não lançado]

### Novidades
- **Galaxy Buds: cada fone e o estojo** (Buds+ e mais novos), pelo canal próprio dos fones: uma linha com o fone
  que acaba primeiro, cada fone e o estojo no status (opcional, desligado por padrão; o canal é aberto no máximo a
  cada 10 minutos). Testado com o Galaxy Buds2.

## [1.2.0] — 25/11/2026

### Novidades
- **Controles PlayStation** pelo Bluetooth: DualSense, DualSense Edge e DualShock 4 *(ainda não testado)*.
- **AirPods e Beats** pelos anúncios Bluetooth LE: uma linha com o fone que acaba primeiro, cada fone e o estojo
  no status (opcional, desligado por padrão; *ainda não testado*).
- **Estimativa de duração** ("~2 dias restantes") pela velocidade com que o nível cai desde a última carga.
- **Aviso de carga completa** quando um dispositivo carregando chega a 100% (ligado por padrão).
- **Um ícone na bandeja por dispositivo** (opcional).
- **Modo portátil**: um `juice.ini` ao lado do `Juice.exe` é usado no lugar de `%APPDATA%\Juice`.
- **Ícone do programa** (a caixinha de suco) e informações de versão no `Juice.exe`.
- Compilação das releases pelo **GitHub Actions**, com `Juice.exe.sha256`; manifestos do **winget**.

### Mudanças
- **Configurações** reorganizadas em seções (Dispositivos, Ícones, Atualização e avisos, Geral), seguindo o tema
  claro/escuro do Windows; a lista de dispositivos tem só os nomes.
- As caixas JBL são procuradas por ~5 s a cada atualização (antes ~2,5 s, que às vezes não achavam a caixa).

## [1.1.0] — 24/11/2026

### Novidades
- **Caixas de som JBL** pelo Bluetooth LE (opcional, desligado por padrão: *Configurações → Ler bateria de
  caixas de som JBL*). Testado com a JBL Flip 7.
- **Controle Xbox Wireless** pelo Bluetooth, a partir do relatório de bateria que o controle envia sozinho
  (4 níveis: 10/40/70/100%, carregando quando alimentado pelo USB).
- **Último nível conhecido** dos dispositivos desconectados, em cinza e com há quanto tempo foi lido, guardado
  entre reinícios (*Configurações → Mostrar o último nível de dispositivos desconectados*, ligado por padrão).
- **Remover um dispositivo** da lista: passe o mouse sobre um dispositivo desconectado e clique no **×**.
- Novas ilustrações e ícones da bandeja de **caixa de som** e **controle**.
- O `probe.exe` também lista caixas JBL, controles de Xbox e o número de série dos dispositivos Logitech.

### Mudanças
- O tipo de cada dispositivo é lembrado, então um dispositivo desconectado mantém o ícone (ex.: um headset
  desligado).
- Dispositivos Logitech são identificados pelo número de série: um mouse usado pelo dongle e pelo Bluetooth
  (como o G309) vira uma linha só, e a entrada de bateria Bluetooth do Windows é unificada a ele.
- Um dispositivo pareado mas não conectado só aparece depois de se conectar pelo menos uma vez.

### Correções
- Dois receptores Logitech do mesmo modelo (ex.: dois dongles LIGHTSPEED) usavam a mesma chave, e um mouse
  escondia o outro.
- Nome e tipo de um dispositivo que responde mas não tem leitura de bateria (headset desligado) agora são
  guardados.

### Testado
- Galaxy Buds2 e Galaxy Buds3 Pro (Bluetooth), G309 (LIGHTSPEED e Bluetooth), JBL Flip 7, controle do Xbox
  One S.

## [1.0.0] — 24/11/2026

Primeira versão: bateria de dispositivos Logitech HID++ e fones Bluetooth no botão da barra de tarefas (barra
de progresso e selo), janela compacta, ícone na bandeja ao fechar, aviso de bateria baixa, dispositivo de menor
bateria / fixo / alternando, inglês e português.

[1.2.0]: https://github.com/gSalzmann/juice/releases/tag/v1.2.0
[1.1.0]: https://github.com/gSalzmann/juice/releases/tag/v1.1.0
[1.0.0]: https://github.com/gSalzmann/juice/releases/tag/v1.0.0

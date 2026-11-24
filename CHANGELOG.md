# Changelog

🇺🇸 English · 🇧🇷 [Português](#-português)

All notable changes to Juice. Versions follow [Semantic Versioning](https://semver.org/).

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

[1.1.0]: https://github.com/gSalzmann/juice/releases/tag/v1.1.0
[1.0.0]: https://github.com/gSalzmann/juice/releases/tag/v1.0.0

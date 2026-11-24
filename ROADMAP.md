# Roadmap

🇺🇸 English · 🇧🇷 [Português](#-português)

What is planned for Juice, in order. Everything keeps the same premise: a single small `.exe`, no
background service, no internet access.

| # | Item | Status |
|---|---|---|
| 1 | **Program icon and version info** in `Juice.exe` (the juice box logo; version, description and copyright in *Properties*) | ✅ done |
| 2 | **PlayStation controllers** (DualSense, DualShock 4) over Bluetooth, from the battery field of their input report | ✅ done · *untested, no hardware* |
| 3 | **AirPods and other Apple earbuds**, from the Bluetooth LE advertisements they broadcast (optional, off by default; one row with the lowest earbud, both earbuds and the case in the status) | ✅ done · *untested, no hardware* |
| 4 | **Time remaining estimate** ("~2 days left") from how fast the level drops | ✅ done |
| 5 | **Full charge notification** when a charging device reaches 100% (on by default) | ✅ done |
| 6 | **One tray icon per device** (optional) | ✅ done |
| 7 | **Portable mode**: a `juice.ini` next to `Juice.exe` is used instead of `%APPDATA%\Juice` | ✅ done |
| 8 | **Automatic builds** with GitHub Actions: release `Juice.exe` built from the source by GitHub, with its SHA-256 | ✅ done |
| 9 | **winget**: `winget install Juice` | planned |

Not planned: automatic update checks (Juice never touches the internet) and Razer / Corsair / SteelSeries
devices (undocumented protocols, one per brand).

---

## 🇧🇷 Português

O que está planejado para o Juice, em ordem. Tudo mantém a mesma premissa: um único `.exe` pequeno, sem
serviço em segundo plano e sem acesso à internet.

| # | Item | Status |
|---|---|---|
| 1 | **Ícone e informações de versão** no `Juice.exe` (o logo da caixinha de suco; versão, descrição e copyright em *Propriedades*) | ✅ feito |
| 2 | **Controles PlayStation** (DualSense, DualShock 4) pelo Bluetooth, a partir do campo de bateria do relatório de entrada | ✅ feito · *não testado, sem o aparelho* |
| 3 | **AirPods e outros fones da Apple**, pelos anúncios Bluetooth LE que eles transmitem (opcional, desligado por padrão; uma linha com o fone de menor nível, os dois fones e o estojo no status) | ✅ feito · *não testado, sem o aparelho* |
| 4 | **Estimativa de duração** ("~2 dias restantes") pela velocidade com que o nível cai | ✅ feito |
| 5 | **Aviso de carga completa** quando um dispositivo carregando chega a 100% (ligado por padrão) | ✅ feito |
| 6 | **Um ícone na bandeja por dispositivo** (opcional) | ✅ feito |
| 7 | **Modo portátil**: um `juice.ini` ao lado do `Juice.exe` é usado no lugar de `%APPDATA%\Juice` | ✅ feito |
| 8 | **Compilação automática** com GitHub Actions: o `Juice.exe` das releases compilado pelo GitHub a partir do código, com o SHA-256 | ✅ feito |
| 9 | **winget**: `winget install Juice` | planejado |

Fora do plano: verificação automática de atualizações (o Juice nunca acessa a internet) e dispositivos
Razer / Corsair / SteelSeries (protocolos não documentados, um por marca).

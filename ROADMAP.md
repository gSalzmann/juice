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
| 9 | **winget**: `winget install gSalzmann.Juice` | 🟡 submitted ([microsoft/winget-pkgs#489217](https://github.com/microsoft/winget-pkgs/pull/489217)), validation passed, waiting for a moderator |
| 10 | **Galaxy Buds: each earbud and the case** (left, right and case levels, like the AirPods), from the protocol Samsung earbuds use over Bluetooth (optional, off by default) | ✅ done · tested with the Galaxy Buds2 |
| 11 | **Charge tonight reminder**: at a chosen time, one notification with the devices below a level ("Charge tonight: G309 (25%), JBL (30%)") | 📋 planned |
| 12 | **Level history**: a small chart of the last days when a device is clicked (one reading per hour in a small file) | 📋 planned |
| 13 | **`juice --list`**: the levels as text or JSON in the terminal, for scripts, Rainmeter or a shell prompt | 📋 planned |
| 14 | **Razer, SteelSeries (Arctis) and HyperX** wireless headsets and mice, one protocol per brand | 📋 planned · *will be untested, no hardware* |

Not planned: automatic update checks (Juice never touches the internet; winget handles updates).

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
| 9 | **winget**: `winget install gSalzmann.Juice` | 🟡 enviado ([microsoft/winget-pkgs#489217](https://github.com/microsoft/winget-pkgs/pull/489217)), validação aprovada, aguardando um moderador |
| 10 | **Galaxy Buds: cada fone e o estojo** (nível do esquerdo, do direito e do estojo, como nos AirPods), pelo protocolo que os fones da Samsung usam no Bluetooth (opcional, desligado por padrão) | ✅ feito · testado com o Galaxy Buds2 |
| 11 | **Lembrete para carregar à noite**: num horário escolhido, um aviso com os dispositivos abaixo de um nível ("Carregue esta noite: G309 (25%), JBL (30%)") | 📋 planejado |
| 12 | **Histórico do nível**: um gráfico pequeno dos últimos dias ao clicar num dispositivo (uma leitura por hora num arquivo pequeno) | 📋 planejado |
| 13 | **`juice --list`**: os níveis em texto ou JSON no terminal, para scripts, Rainmeter ou o prompt do shell | 📋 planejado |
| 14 | **Razer, SteelSeries (Arctis) e HyperX**: fones e mouses sem fio, um protocolo por marca | 📋 planejado · *ficará sem teste, sem os aparelhos* |

Fora do plano: verificação automática de atualizações (o Juice nunca acessa a internet; o winget cuida das atualizações).

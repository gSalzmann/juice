# Wireless Tray

Programa super leve (Win32/C++ puro, sem runtime) que mostra na bandeja do Windows o nível de bateria
de mouses, teclados e fones sem fio — sem depender de software do fabricante (G HUB etc.).

- Um ícone por dispositivo: símbolo do tipo (mouse/fone/teclado) + percentual
- Cor do número: verde carregando, vermelho abaixo de 15%; opcionalmente (configurações)
  azul ≥ 30% e laranja entre 15% e 29%, como a bateria de notebook no Windows
- Aviso de bateria baixa (notificação do Windows)
- Tela de configuração: dispositivos visíveis, intervalo de atualização, limite de aviso, iniciar com o Windows
- ~0,5 MB de working set, executável de ~200 KB

## Wireless Bar (barra de tarefas)

Alternativa à bandeja: `WirelessBar.exe` fica aberto como um programa normal, com um botão na barra de
tarefas que mostra a bateria de um dispositivo:

- Barra de progresso do botão = % de bateria (azul ≥ 30%, amarelo 15–29%, vermelho < 15%); na janela e no
  selo, verde quando carregando
- Selo no ícone com o número (ou um raio quando carregando); o título mostra "Nome — 97%"
- Ícone do botão com ilustração do tipo de dispositivo
- Dispositivo exibido: automático (menor bateria), todos (alterna entre os conectados a cada minuto) ou um
  escolhido nas configurações
- Opção para ocultar o número do selo (fica só a barra de progresso; o raio continua aparecendo ao carregar)
- Clique no botão abre uma janela compacta com todos os dispositivos, barra de nível e status
- Configurações próprias em `%APPDATA%\WirelessTray\bar.ini`; "Iniciar com o Windows" abre minimizado
- Modo de teste: `WirelessBar.exe --demo 22` (ou `--demo 80c` para carregando) simula o nível do dispositivo da
  barra; com a janela em foco, ↑/↓ mudam 5% e C liga/desliga o carregamento. Abre um botão separado do app normal

## Dispositivos suportados

| Fonte | Como | Testado com |
|---|---|---|
| Logitech HID++ 2.0 (receptores LIGHTSPEED / Unifying / Bolt, USB direto) | features `0x1004`, `0x1000`, `0x1001`, `0x1F20` | G502 X PLUS (LIGHTSPEED), G933 |
| Bluetooth clássico (fones com indicador de bateria HFP) | propriedade de bateria do stack Bluetooth do Windows | Galaxy Buds2 (pendente) |

Dispositivos Logitech em repouso (mouse parado) não respondem; o último valor é mantido por até 1 hora.

## Compilar

Requer Visual Studio Build Tools 2022 (workload "Desenvolvimento para desktop com C++").

```bat
build.bat
```

Gera em `build\`:

- `WirelessTray.exe` — o app de bandeja
- `WirelessBar.exe` — o app da barra de tarefas
- `probe.exe` — diagnóstico em linha de comando (lista dispositivos e baterias)
- `iconpreview.exe` — renderiza os ícones da bandeja ampliados num BMP (desenvolvimento)
- `illustpreview.exe` — renderiza as ilustrações e selos da barra de tarefas num BMP (desenvolvimento)

## Configuração

Salva em `%APPDATA%\WirelessTray\config.ini`. "Iniciar com o Windows" usa a chave
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (não requer administrador).

No Windows 11, ícones novos vão para a área oculta da bandeja; para fixá-los, use
*Configurações → Personalização → Barra de tarefas → Outros ícones da bandeja do sistema*.

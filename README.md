# Wireless Tray

Programa super leve (Win32/C++ puro, sem runtime) que mostra o nível de bateria de mouses, teclados e fones
sem fio no Windows — sem depender de software do fabricante (G HUB etc.).

`WirelessBar.exe` funciona como um programa normal, com um botão na barra de tarefas; ao fechar a janela, ele
continua rodando na bandeja.

## Barra de tarefas (janela aberta ou minimizada)

- Barra de progresso do botão = % de bateria (azul ≥ 30%, amarelo 15–29%, vermelho < 15%); na janela e no
  selo, verde quando carregando
- Selo no ícone com o número (ou um raio quando carregando); o título mostra "Nome — 97%".
  O número pode ser ocultado nas configurações (o raio continua aparecendo ao carregar)
- Ícone do botão com ilustração do tipo de dispositivo
- Dispositivo exibido: automático (menor bateria), todos (alterna entre os conectados a cada minuto) ou um
  escolhido nas configurações
- Clique no botão abre uma janela compacta com todos os dispositivos, barra de nível e status

## Bandeja (janela fechada)

- Fechar a janela (X) leva o app para a bandeja; o ícone mostra o mesmo dispositivo da barra de tarefas
  (símbolo do tipo + percentual) e o tooltip lista todos
- Cor do número: verde carregando, vermelho abaixo de 15%; opcionalmente azul ≥ 30% e laranja entre 15% e 29%
- Clique abre a janela de novo; botão direito: Abrir, Atualizar agora, Configurações, Sair
- "Iniciar com o Windows" abre direto na bandeja

## Geral

- Aviso de bateria baixa (notificação do Windows)
- Configurações em `%APPDATA%\WirelessTray\bar.ini`: dispositivo da barra, dispositivos visíveis, intervalo de
  atualização, limite de aviso, número no selo, cor do número na bandeja, iniciar com o Windows
  (chave `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`, não requer administrador)
- Modo de teste: `WirelessBar.exe --demo 22` (ou `--demo 80c` para carregando) simula o nível do dispositivo da
  barra; com a janela em foco, ↑/↓ mudam 5% e C liga/desliga o carregamento. Abre um botão separado do app normal
- Poucos MB de memória com a janela aberta; ~0,2 MB na bandeja

No Windows 11, ícones novos vão para a área oculta da bandeja; para fixá-lo, use
*Configurações → Personalização → Barra de tarefas → Outros ícones da bandeja do sistema*.

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

- `WirelessBar.exe` — o app
- `probe.exe` — diagnóstico em linha de comando (lista dispositivos e baterias)
- `iconpreview.exe` — renderiza os ícones da bandeja ampliados num BMP (desenvolvimento)
- `illustpreview.exe` — renderiza as ilustrações e selos da barra de tarefas num BMP (desenvolvimento)

# Wireless Tray

Programa super leve (Win32/C++ puro, sem runtime) que mostra na bandeja do Windows o nível de bateria
de mouses, teclados e fones sem fio — sem depender de software do fabricante (G HUB etc.).

- Um ícone por dispositivo: símbolo do tipo (mouse/fone/teclado) + percentual
- Cores: amarelo abaixo de 30%, vermelho abaixo do limite de aviso, verde carregando
- Aviso de bateria baixa (notificação do Windows)
- Tela de configuração: dispositivos visíveis, intervalo de atualização, limite de aviso, iniciar com o Windows
- ~0,5 MB de working set, executável de ~200 KB

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
- `probe.exe` — diagnóstico em linha de comando (lista dispositivos e baterias)
- `iconpreview.exe` — renderiza os ícones ampliados num BMP (desenvolvimento)

## Configuração

Salva em `%APPDATA%\WirelessTray\config.ini`. "Iniciar com o Windows" usa a chave
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (não requer administrador).

No Windows 11, ícones novos vão para a área oculta da bandeja; para fixá-los, use
*Configurações → Personalização → Barra de tarefas → Outros ícones da bandeja do sistema*.

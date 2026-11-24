<p align="center">
  <a href="README.md">English</a> · <b>Português (Brasil)</b>
</p>

<p align="center">
  <img src="docs/img/banner.pt-BR.svg" alt="Juice — a carga da bateria enchendo como suco" width="900">
</p>

<p align="center">
  <b>A bateria dos seus periféricos sem fio, direto na barra de tarefas do Windows.</b><br>
  Sem G HUB, sem serviço em segundo plano, sem instalador — um único <code>.exe</code> de ~270 KB.
</p>

<p align="center">
  <img alt="Windows 10/11" src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078D4?logo=windows&logoColor=white">
  <img alt="C++17 Win32" src="https://img.shields.io/badge/C%2B%2B17-Win32%20puro-F4600C?logo=cplusplus&logoColor=white">
  <img alt="Sem dependências" src="https://img.shields.io/badge/depend%C3%AAncias-nenhuma-2EA043">
  <img alt="Memória" src="https://img.shields.io/badge/RAM-~0%2C2%20MB%20na%20bandeja-FFC93C">
  <a href="LICENSE"><img alt="Licença MIT" src="https://img.shields.io/badge/licen%C3%A7a-MIT-8B949E"></a>
</p>

---

## Por que "Juice"?

Em inglês, **juice** é gíria para *carga de bateria* — quem diz *"my phone is running out of juice"* está
dizendo que o celular está ficando sem bateria. O logo brinca com isso: a bateria é uma caixinha de suco
com canudo, e a carga é o suco que enche a caixinha (e as letras do nome) até aparecer o raio de carga cheia.

## O que ele faz

O Juice lê o nível de bateria de mouses, teclados e headsets sem fio — direto do hardware, sem o software do
fabricante — e mostra no lugar mais visível do Windows: **o botão do próprio app na barra de tarefas**.

- 🔋 **Barra de progresso do botão = % de bateria**, colorida pela faixa de carga
- 🔢 **Selo no ícone** com o número (ou um raio quando está carregando)
- 🖱️ **Ilustração do dispositivo** como ícone do botão (mouse, headset, teclado)
- 🪟 **Janela compacta** com todos os dispositivos, nível, status e horário da última leitura
- 📥 **Fechou a janela? Vai para a bandeja**, com o ícone do dispositivo e o percentual em pixel-art
- 🔔 **Aviso de bateria baixa** com notificação do Windows
- 🔁 **Vários dispositivos**: mostra o de menor bateria, um fixo, ou alterna entre todos a cada minuto
- 🪶 **Leve de verdade**: Win32 puro, sem runtime, ~0,2 MB de memória na bandeja

## Como fica

### A janela

<img src="docs/img/window.png" alt="Janela do Juice com o G502 X PLUS a 92% e o G933 desconectado" width="342">

Cada linha mostra o dispositivo, o percentual, uma barra no tom da faixa de carga e o status
(*Atualizado agora*, *Carregando*, *Em repouso · leitura há 12 min*, *Desconectado*). O dispositivo
marcado com **· na barra de tarefas** é o que está no botão. Atalhos: <kbd>F5</kbd> atualiza,
<kbd>Esc</kbd> minimiza.

### O botão na barra de tarefas

| Normal | Bateria baixa | Crítica | Carregando |
|:---:|:---:|:---:|:---:|
| <img src="docs/img/taskbar-97.png" width="120" alt="97%, barra azul"> | <img src="docs/img/taskbar-22.png" width="120" alt="22%, barra amarela"> | <img src="docs/img/taskbar-9.png" width="120" alt="9%, barra vermelha"> | <img src="docs/img/taskbar-64c.png" width="120" alt="carregando, selo verde com raio"> |
| ≥ 30% · azul | 15–29% · amarelo | < 15% · vermelho | qualquer nível · verde + ⚡ |

A barra de progresso usa os estados nativos do Windows (normal, pausado e erro), então o azul acompanha a
cor de destaque do sistema. O selo pode ser desligado nas configurações — aí fica só a barra (o raio de
carregamento continua aparecendo).

> Capturas reais do Windows 11. O título da janela também mostra o valor: `G502 X PLUS — 92%`.

### A bandeja

Ao **fechar** a janela (X), o Juice continua rodando na área de notificação. O ícone mostra o mesmo
dispositivo da barra de tarefas, com o número desenhado em dígitos de 4×6 pixels para ficar legível em 16 px:

<img src="docs/img/tray-icons.png" alt="Ícones da bandeja: mouse 97, headset 37, headset 22, teclado 12, mouse carregando 55" width="720">

<sub>Ampliado 10×. Com "Colorir o número" ligado: azul ≥ 30%, laranja 15–29%, vermelho &lt; 15%, verde carregando.
Desligado, o número segue o tema da barra e só fica vermelho no nível crítico e verde ao carregar.</sub>

- **Clique** no ícone: reabre a janela
- **Botão direito**: Abrir · Atualizar agora · Configurações · Sair
- Passe o mouse para ver **todos** os dispositivos no tooltip

### As ilustrações

Desenhadas em código por um pequeno renderizador vetorial (SDF com anti-aliasing e gradientes), nítidas
em qualquer tamanho e escala de tela, nos temas escuro e claro:

<img src="docs/img/illustrations.png" alt="Ilustrações de mouse, headset, teclado e bateria em 24, 32 e 64 px, e os selos 97, 22, 9, 100 e carregando" width="900">

## Fluxo da janela

```mermaid
flowchart LR
    A([Juice.exe]) --> J[Janela aberta]
    J -- "minimizar ( — )" --> B[Botão na barra de tarefas<br/>barra de progresso + selo]
    B -- clique --> J
    J -- "fechar ( X )" --> T[Ícone na bandeja<br/>dispositivo + %]
    T -- clique / Abrir --> J
    T -- "botão direito → Sair" --> S([Encerrado])
    W([Iniciar com o Windows]) --> T
```

Abrir o `Juice.exe` de novo com ele já rodando apenas traz a janela existente de volta.

## Dispositivos suportados

| Fonte | Como o Juice lê | Testado com |
|---|---|---|
| **Logitech HID++ 2.0** — receptores LIGHTSPEED, Unifying, Bolt e USB direto | features `0x1004` (UnifiedBattery), `0x1000`, `0x1001` (tensão → %) e `0x1F20` (headsets) | G502 X PLUS (LIGHTSPEED), G933 |
| **Bluetooth clássico** — fones com indicador de bateria (HFP) | propriedade de bateria que o próprio stack Bluetooth do Windows expõe | Galaxy Buds2 *(ainda não testado)* |

O tipo de cada dispositivo (mouse, teclado, headset) e o nome são descobertos automaticamente — via feature
`0x0005` do HID++ ou pela classe do dispositivo Bluetooth.

**Dispositivos que dormem:** mouses Logitech parados entram em repouso e param de responder. O Juice mantém
o último valor por até 1 hora, marcado como *Em repouso · leitura há X min*, em vez de mostrar o
dispositivo como desconectado.

**Atualização:** a leitura roda numa thread separada no intervalo configurado, e também logo depois de
conectar/desconectar um dispositivo ou de o PC voltar da suspensão.

## Configurações

Clique em **Configurações** na janela ou no menu da bandeja.

| Opção | Padrão | O que faz |
|---|---|---|
| Dispositivo mostrado na barra de tarefas | Automático | *Automático* = o de menor bateria (prioriza os que não estão carregando) · *Todos* = alterna entre os conectados a cada minuto · ou um dispositivo fixo |
| Dispositivos exibidos na janela | todos | Desmarque para esconder um dispositivo da janela, da barra e da bandeja |
| Atualizar a cada | 5 min | Intervalo entre leituras (1–60 min) |
| Avisar quando a bateria ficar abaixo de | 15% | Notificação do Windows, uma vez por descarga |
| Exibir o percentual no ícone da barra de tarefas | ligado | Liga/desliga o selo com o número |
| Colorir o número do ícone da bandeja conforme o nível | desligado | Azul / laranja / vermelho em vez da cor neutra |
| Iniciar com o Windows (na bandeja) | desligado | Usa `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` — não precisa de administrador |

Tudo fica em `%APPDATA%\Juice\juice.ini` (texto simples, pode ser apagado para voltar ao padrão).

> **Windows 11:** ícones novos da bandeja vão para a área oculta (^). Para deixar o Juice sempre visível:
> *Configurações → Personalização → Barra de tarefas → Outros ícones da bandeja do sistema*.

## Linha de comando

| Argumento | Efeito |
|---|---|
| `--tray` | Inicia direto na bandeja (é o que o "Iniciar com o Windows" usa) |
| `--demo 22` | Modo de teste: simula 22% no dispositivo da barra, num botão separado do app normal |
| `--demo 80c` | Idem, carregando |

No modo de teste, com a janela em foco, <kbd>↑</kbd>/<kbd>↓</kbd> mudam o nível em 5% e <kbd>C</kbd>
liga/desliga o carregamento — útil para ver todas as cores e o aviso de bateria baixa sem esperar a
bateria descarregar.

## Compilar

Requisitos: **Visual Studio Build Tools 2022** com o workload *Desenvolvimento para desktop com C++*.

```bat
build.bat
```

O script encontra o MSVC sozinho (via `vswhere`) e gera em `build\`:

| Arquivo | Para quê |
|---|---|
| `Juice.exe` | o app |
| `probe.exe` | diagnóstico no terminal: lista cada interface HID++, dispositivos Bluetooth e baterias lidas |
| `iconpreview.exe` | renderiza os ícones da bandeja ampliados num BMP |
| `illustpreview.exe` | renderiza as ilustrações e os selos num BMP |

Compilado com `/O1 /GL /LTCG` e CRT estática (`/MT`): não precisa de nenhum redistribuível instalado.

### Estrutura

```
src/
  juice.cpp      janela, botão da barra de tarefas, bandeja, configurações
  devices.*      lista de dispositivos, leitura em segundo plano, repouso e avisos
  hidpp.*        protocolo Logitech HID++ 2.0 sobre HID do Windows
  btbattery.*    bateria de dispositivos Bluetooth via propriedades do sistema
  illust.*       renderizador SDF: ilustrações, selos e barras da janela
  icon.*         ícones da bandeja (glifo + dígitos 4×6)
  config.*       juice.ini e início automático
tools/           probe e geradores de preview
docs/img/        imagens deste README
```

## Diagnóstico

Se um dispositivo não aparecer, rode `build\probe.exe` com ele ligado. A saída mostra cada receptor e slot
encontrado, as features HID++ usadas e o resultado da leitura — por exemplo `no response (asleep?)` para um
mouse em repouso, ou o erro devolvido por um headset desligado.

## Privacidade

O Juice não acessa a internet, não coleta nada e não instala serviços. Ele só conversa com os seus
dispositivos pelas APIs HID e Bluetooth do próprio Windows e grava um arquivo `.ini` na sua pasta de usuário.

## Licença

[MIT](LICENSE) — use, modifique e distribua à vontade, mantendo o aviso de copyright.

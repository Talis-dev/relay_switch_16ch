# Relay Switch 5CH + 2DI — WT32-ETH01 V1.4

Módulo remoto com cinco relés, duas entradas digitais em PCF8574, Ethernet LAN8720 integrada, API REST, MQTT, webhook opcional e painel web. Mantém OTA e Modbus TCP do projeto original.

O sketch principal agora é `relay_switch_5ch_ethernet.ino`, com o mesmo nome da pasta para compilar no Arduino IDE. A configuração usa o namespace NVS `relay5`; configurações antigas de 16 canais não são importadas.

## Ligações previstas

Hipóteses adotadas: **WT32-ETH01 V1.4**, PCF8574 em `0x20`, relés ativos em LOW e entradas com contato para GND. Confirmar na placa antes de ligar cargas. Endereço e polaridade das entradas são configuráveis.

| Função | Ligação |
|---|---|
| LAN8720 integrado | PHY 1, MDC 23, MDIO 18, power 16, clock externo GPIO0 |
| SDA / SCL | GPIO33 / GPIO32 (podem estar marcados 485_EN / CFG) |
| Relés 1–5 | PCF8574 P0–P4, ativos em LOW |
| Entradas 1–2 | PCF8574 P5–P6 |
| Reserva | P7, mantido em HIGH |
| Habilitação física dos cinco drivers | GPIO4, EN ativo em HIGH, pull-down externo de 10 kΩ |
| Endereço padrão | 0x20, A0/A1/A2 em GND |

O PCF8574 transfere **um byte**, diferente do PCF8575 original. P5/P6/P7 permanecem em HIGH em todas as escritas, liberando os pinos de entrada. O boot escreve `0xFF`, desligando os cinco relés; não restaura saídas energizadas após reinício.

### Partida sem pulso: circuito obrigatório na PCB

O PCF8574 inicia com os pinos em HIGH, por isso o acionamento foi definido como
LOW = ligado. Contudo, se somente o ESP reiniciar, o expansor pode continuar
alimentado e manter uma saída antiga em LOW. Para impedir esse acionamento
durante a inicialização, há uma habilitação independente dos cinco drivers:

- GPIO4 = LOW ou flutuante: etapa dos optoacopladores bloqueada, todos os relés OFF.
- GPIO4 = HIGH: permite os comandos individuais P0–P4.
- Pull-down físico de 10 kΩ de GPIO4 para GND: bloqueia antes do firmware executar.
- EN só sobe após o PCF aceitar a escrita com todos os relés OFF no boot.
- Falha de leitura/escrita I²C derruba EN. A recuperação reescreve a máscara
  comandada antes de liberar EN. Reboot solicitado e início de OTA bloqueiam EN.

Uma implementação possível é um interruptor de alimentação **high-side** nos
ânodos dos cinco optoacopladores, comandado por EN ativo em HIGH (por exemplo,
P-MOSFET com gate puxado ao source e NPN controlado pelo GPIO4). Cada canal mantém
seu resistor e o cátodo conectado a P0–P4. Use driver adequado à corrente e MOSFET
especificado para a tensão de gate utilizada. Não deixe a alimentação dos LEDs
permanentemente ligada contornando essa habilitação.

Firmware sozinho não garante ausência absoluta de pulsos sob qualquer falha de
alimentação. A garantia depende desse bloqueio físico, do dimensionamento dos
drivers e da sequência das fontes. Para brownout/energização parcial, use
supervisão de tensão que também mantenha EN bloqueado até as fontes estabilizarem.
Valide com osciloscópio em power-on, reset, bootloader, gravação, OTA e rampas
de alimentação antes de fechar a PCB. Referências: [TI PCF8574](https://www.ti.com/lit/ds/symlink/pcf8574.pdf)
e [estados dos GPIOs da Espressif](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32/schematic-checklist.html).

Alimente a lógica do PCF8574 em **3,3 V**, com pull-ups de SDA/SCL para 3,3 V. Confira os pull-ups de módulos prontos. Os 12 V são da etapa dos relés: nunca ligar 12 V ao ESP32 ou ao PCF8574. A placa precisa de drivers de bobina e supressão indutiva; o expansor aciona somente o estágio dos optoacopladores, dimensionado para sua corrente de saída. Entradas externas em 12 V precisam de condicionamento/optoacoplador; contato seco ou coletor aberto para GND é a hipótese do firmware. Dimensione isolação, fontes, desacoplamento e retorno das bobinas na PCB.

O estado informado é **comandado**, sem feedback dos contatos. Falhas I²C retornam HTTP 503, mas a máscara solicitada fica em RAM e é reaplicada quando a comunicação volta. Não representa confirmação física da carga.

## Painel e entradas

Inicia em DHCP. Consulte o IP na serial a 115200 baud e abra `http://IP/`. O painel não precisa de CDN ou internet. Oferece controle dos relés, entradas, status Ethernet/MQTT/Wi-Fi/webhook, scan I²C e formulários de configuração.

As entradas são amostradas pelo loop em intervalos mínimos de 10 ms, com debounce padrão de 50 ms por canal. Não são contadores de pulsos: HTTP local/MQTT síncronos, OTA e falhas de rede podem aumentar esse intervalo. O primeiro estado estabelece a base e não dispara webhook. Cada mudança estável gera evento, inclusive na volta ao estado inativo. Em falha I²C a leitura fica inválida; o último estado é comparado após recuperação e novo debounce.

API e exemplos: [docs/API.md](docs/API.md), também disponíveis pelo link
**Documentação da API** no painel ou diretamente em `http://IP/api/docs`.
A página está embarcada e funciona sem internet; os exemplos mostram o IP de acesso atual.
Ao editar a documentação, execute `node scripts/build-api-docs.cjs` antes de compilar.
Para verificar a sincronização: `node scripts/build-api-docs.cjs --check`.

## Webhook opcional

Configure URL HTTP/HTTPS e token Bearer opcional. HTTPS exige CA PEM (máximo 2048 bytes) e horário NTP sincronizado. Não há TLS inseguro. NTP usa `pool.ntp.org` e `time.google.com`; redes isoladas precisam ajustar o sketch ou usar HTTP na rede local protegida.

Uma tarefa independente envia os POSTs. Fila de oito eventos aguardando, além de um em processamento. Cada evento captura URL, credenciais e estado no momento da mudança. Alterações de configuração afetam eventos novos; os enfileirados mantêm o destino anterior. Até três tentativas; 4xx são terminais exceto 408/429. Não segue redirecionamentos.

`event_id` e `X-Event-ID` permanecem iguais nas tentativas: deduplique no servidor, pois um timeout pode ocorrer após processar o POST. A fila é volátil; reinício, fila cheia ou tentativas esgotadas podem perder eventos. `health.webhook` informa entregas, falhas e descartes. Use GET `/api/v1/inputs` para reconciliar o estado atual.

## Wi-Fi para o ESP do display

É viável usar o WT32 como **ponto de acesso Wi-Fi com NAT pela Ethernet**:

```text
ESP do display -- Wi-Fi --> WT32-ETH01 -- Ethernet --> rede --> servidor API
                              |
                         5 relés + 2 DI
```

O core Arduino-ESP32 3.3.3 inclui o [exemplo oficial ETH_WIFI_BRIDGE](https://github.com/espressif/arduino-esp32/blob/3.3.3/libraries/Ethernet/examples/ETH_WIFI_BRIDGE/ETH_WIFI_BRIDGE.ino). Este projeto aplica o recurso de NAT ao LAN8720 da WT32.

Wi-Fi inicia **desabilitado**. Configure SSID, senha de 8–63 bytes, canal, IP e DNS, habilite e reinicie. Rede padrão: `192.168.4.0/24`, WT32 em `192.168.4.1`, DHCP para até dois clientes conectados. O DNS precisa estar acessível pela Ethernet; use o DNS interno para nomes do servidor local. Conecte o display a esse SSID mantendo a URL do servidor.

As sub-redes Ethernet e Wi-Fi devem ser diferentes. Em conflito detectado após DHCP, o NAT fica desabilitado e o painel indica `subnet_conflict`. O NAT acompanha o link Ethernet e tenta se recuperar quando ele volta.

GET/POST iniciados pelo display podem atravessar o NAT, inclusive HTTPS validado pelo display. Não requer reescrever APIs como proxy no WT32. Não há encaminhamento de portas, descoberta broadcast entre redes ou conexão iniciada pelo servidor para o display. O NAT dá acesso geral à rede alcançável; não restringe a um servidor. Destina-se a poucos clientes e baixo tráfego, sujeito a teste de alcance, latência e reconexão.

**Remover o roteador só funciona se a Ethernet continuar fornecendo caminho ao servidor.** Se o roteador também fornece internet, gateway ou DHCP da rede, essas funções precisam permanecer a montante ou ser substituídas. O WT32 fornece AP/DHCP aos clientes Wi-Fi, sem criar internet na ausência de uplink.

## Alcance Wi-Fi e antena

Na PCB principal, deixe a região da antena impressa do módulo fora da placa sempre
que possível, sem cobre, cabos, relés ou partes metálicas próximos. A Espressif
recomenda espaço livre de pelo menos 15 mm ao redor da antena dentro do gabinete.

Uma antena externa de 2,4 GHz pode ser usada em um caminho de RF devidamente
adaptado, mas soldar um fio em um ponto qualquer da antena impressa pode piorar o
casamento e o alcance. A adaptação exige identificar o ponto correto de RF,
isolar a antena original e manter uma conexão coaxial de 50 Ω curta, com retorno
de terra junto ao ponto de conexão. A placa WT32-ETH01 V1.4 ainda não foi
inspecionada para determinar o ponto físico de modificação; este projeto não
especifica local de corte ou solda. Para uma placa definitiva com antena externa,
prefira hardware projetado para esse conector e valide o desempenho no gabinete.

Referência: [diretrizes de layout RF da Espressif](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32/pcb-layout-design.html).

## MQTT e Modbus

MQTT desabilitado por padrão. Prefixo: `industrial/relay5/relay-switch-5ch`.

| Tópico relativo | Uso |
|---|---|
| `relay/1/set` … `relay/5/set` | ON/OFF ou JSON `{"on":true}` |
| `relays/set` | `{"mask":0}` (0–31) ou cinco `states` |
| `relay/1/state` … `relay/5/state` | ON/OFF retido |
| `relays/state` | Estados comandados, retido |
| `inputs/state` | Entradas, máscara, validade e sequência, retido |
| `availability` | online / LWT offline |
| `health` | Diagnóstico a cada 30 s |
| `config/get`, `config/set`, `config/status` | Consulta/alteração de configuração |

Entradas são publicadas após mudanças e reconexão; não há histórico das bordas durante desconexão. Modbus opcional: coils zero-based `0..4` para relés 1–5. Unit ID é informativo; a biblioteca TCP aceita qualquer Unit ID. Entradas disponíveis por REST/MQTT.

## Compilar e gravar

- Arduino-ESP32 **3.3.3**, `ESP32 Dev Module`, flash 4 MB.
- ArduinoJson **7.4.2**, PubSubClient **2.8**, modbus-esp8266 **4.1.0**.
- Partição **Minimal SPIFFS (1.9 MB APP with OTA)**, `min_spiffs`. A partição padrão de 1.3 MB pode ser insuficiente com Wi-Fi/TLS. Não use Huge APP, que elimina OTA.

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs --build-path .build .
```

Primeira gravação por USB-UART com níveis de 3,3 V e procedimento de boot da WT32. Depois OTA por `relay-switch-5ch.local` ou IP Ethernet. Alteração da tabela de partições exige gravação serial inicial com a tabela; OTA atualiza somente a aplicação. Não envie a imagem a um slot OTA antigo menor.

API HTTP, MQTT, Modbus e ArduinoOTA seguem o modelo local sem autenticação do projeto original. Clientes do AP também acessam o painel e os controles. Use rede de controle confiável, sem publicar essas portas na internet.

## Validação de bancada pendente

Validação local: compilação para ESP32 com `min_spiffs` e cinco testes do
JavaScript do painel com DOM/API simulados (`node --test tests/web-ui.test.cjs`).
Esses testes cobrem comandos, configuração e exibição de falhas; não substituem
teste visual em navegador, comunicação real ou medições elétricas.

Compilação e testes locais não confirmam o hardware. Antes da instalação, confirme versão/pinagem, corrente dos optoacopladores e saídas no boot. Teste cinco saídas simultâneas, mudanças/debounce das entradas, falhas e retorno de I²C/Ethernet, MQTT, webhook com timeout/fila cheia e OTA. No display valide DHCP/DNS, GET/POST reais, HTTPS, keep alive e reconexão após reiniciar cada ESP. Meça a alimentação com Ethernet, Wi-Fi e relés simultâneos.

Referência elétrica: [datasheet TI PCF8574](https://www.ti.com/lit/ds/symlink/pcf8574.pdf).

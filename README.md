# Relay Switch Industrial 16CH — ESP32 ETH01 V4.1

Firmware para acionamento remoto de 16 relés por Ethernet, com OTA, API REST,
MQTT e Modbus TCP. O equipamento inicia em DHCP, mantém o serviço OTA atendido
continuamente e grava as configurações em NVS (memória persistente do ESP32).

## Estado atual

- Ethernet LAN8720 validada com os mesmos parâmetros do sketch inicial.
- OTA sempre iniciado e processado no `loop()`.
- PCF8575 no endereço padrão `0x20`, I²C em SDA 33 / SCL 32.
- Os 16 relés iniciam desligados em todo boot.
- REST na porta 80.
- MQTT desabilitado por padrão e configurável sem reiniciar.
- Modbus TCP desabilitado por padrão; pode ser habilitado pela API ou MQTT.
- Configuração persistente: rede, MQTT, nome do dispositivo e Modbus.
- Health check com Ethernet, heap, PCF8575, MQTT e Modbus.

## Hardware e níveis elétricos

| Função | Configuração |
|---|---|
| PHY Ethernet | LAN8720 integrado |
| PHY address | `1` |
| MDC / MDIO | GPIO 23 / GPIO 18 |
| Power do PHY | GPIO 16 |
| Clock Ethernet | GPIO 0, entrada |
| I²C SDA / SCL | GPIO 33 / GPIO 32 |
| PCF8575 | `0x20` (A0=A1=A2 em LOW) |
| Canal 1…16 | P0…P15 |

O módulo de relés é acionado em nível lógico LOW. Por isso:

- relé lógico `OFF` → saída do PCF8575 em `HIGH`;
- relé lógico `ON` → saída do PCF8575 em `LOW`;
- no início do firmware é escrito `0xFFFF` no PCF8575.

O status informado pelo firmware representa o estado **comandado**. O PCF8575
confirma a comunicação I²C por ACK, mas não mede os contatos físicos do relé.
Para confirmação real de carga/contato seria necessário adicionar entradas de
feedback isoladas.

## Bibliotecas e compilação

Versões usadas na validação:

- ESP32 Arduino core `3.3.3`;
- ArduinoJson `7.4.2`;
- PubSubClient `2.8`;
- modbus-esp8266 `4.1.0`.

Na Arduino IDE, selecione `ESP32 Dev Module` e uma tabela de partições com OTA,
por exemplo a padrão de 4 MB. Não use `Huge APP`, pois ela normalmente remove a
segunda partição necessária para atualização OTA.

Compilação validada:

```text
Flash: 1.120.247 bytes (85% de 1.310.720)
RAM global: 52.420 bytes (15% de 327.680)
```

A biblioteca Modbus 4.1.0 emite avisos de API obsoleta (`available`/`flush`)
com o core 3.3.3, mas a compilação é concluída. Os avisos estão dentro da
biblioteca, não no firmware.

## Primeiro boot e OTA

1. Grave inicialmente por cabo serial/USB usando as configurações já testadas
   para a placa ETH01 V4.1.
2. Conecte o cabo de rede a uma rede com DHCP.
3. Abra o monitor serial em 115200 baud e anote o IP recebido.
4. A porta de rede deve aparecer como `relay-switch-16ch.local` na Arduino IDE.
5. Nos próximos uploads, selecione essa porta de rede e envie normalmente.

O OTA é chamado antes dos demais serviços a cada passagem do `loop`. Tentativas
MQTT têm timeout de conexão de 1 segundo e intervalo de 10 segundos para não
prender continuamente o processador quando o broker estiver indisponível.

## API REST

Base: `http://IP_DO_EQUIPAMENTO/api/v1`

| Método | Rota | Uso |
|---|---|---|
| `GET` | `/health` | Diagnóstico geral |
| `GET` | `/relays` | Estado dos 16 canais |
| `PUT` | `/relays/{1..16}` | Comanda um canal |
| `PUT` | `/relays` | Comanda máscara ou todos os estados |
| `GET` | `/config` | Configuração; senha MQTT vem mascarada |
| `PUT` | `/config` | Atualização parcial persistente |
| `GET` | `/i2c` | Diagnóstico instantâneo, sem acessar o barramento |
| `POST` | `/i2c/scan` | Inicia scan I²C incremental e não bloqueante |
| `POST` | `/reboot` | Reinicia o ESP32 após 500 ms |

Os exemplos completos de payloads e respostas estão em
[`docs/API.md`](docs/API.md).

## MQTT

Prefixo padrão:

```text
industrial/relay16/relay-switch-16ch
```

| Tópico relativo | Direção | Retido | Conteúdo |
|---|---|---:|---|
| `availability` | dispositivo → broker | sim | `online` / LWT `offline` |
| `health` | dispositivo → broker | não | JSON a cada 30 s |
| `relays/state` | dispositivo → broker | sim | estado completo |
| `relay/N/state` | dispositivo → broker | sim | `ON` ou `OFF` |
| `relay/N/set` | broker → dispositivo | não | `ON`, `OFF` ou `{"on":true}` |
| `relays/set` | broker → dispositivo | não | `{"mask":5}` ou `states` |
| `config/get` | broker → dispositivo | não | qualquer payload |
| `config/set` | broker → dispositivo | não | mesmo JSON parcial da REST |
| `config/status` | dispositivo → broker | sim | config mascarada/erro |

QoS solicitado nas assinaturas de controle: 1. As publicações feitas pelo
PubSubClient são QoS 0; estados importantes ficam retidos no broker.

## Modbus TCP

Desabilitado por padrão. Ao habilitar, o servidor abre a porta configurada
(padrão 502) sem criar tarefas extras. Mapa de coils:

| Coil zero-based | Notação comum em supervisórios | Relé |
|---:|---:|---:|
| 0 | 00001 | 1 |
| 1 | 00002 | 2 |
| … | … | … |
| 15 | 00016 | 16 |

Funções usuais suportadas pela biblioteca: leitura de coils `FC01`, escrita de
um coil `FC05` e escrita de vários coils `FC0F`. O campo Unit ID configurável
(padrão 1) deve ser usado no cliente; como é comum em servidores Modbus TCP
diretos, a biblioteca não filtra requisições por Unit ID.

Alterar a porta Modbus exige reinício. Habilitar/desabilitar pode ser feito em
tempo de execução. Depois de um servidor já ter sido aberto, desabilitar suspende
o processamento; a porta é totalmente recriada no próximo reboot.

## Decisões de estabilidade

- Nenhuma tarefa de aplicação foi criada em outro núcleo. OTA, REST, MQTT e
  Modbus executam cooperativamente no mesmo contexto, evitando acesso concorrente
  à pilha TCP/IP.
- O callback de eventos Ethernet apenas atualiza flags e logs curtos.
- Uma alteração MQTT é aplicada depois que o callback atual termina.
- Várias escritas Modbus são consolidadas antes da publicação MQTT de status.
- O controle do PCF8575 não depende de MQTT, REST ou Modbus estarem conectados.
- Em falha I²C, o estado solicitado é preservado e a escrita é repetida a cada
  segundo com timeout de 5 ms; a API responde `503` e o health muda para
  `degraded`.
- O scan I²C testa somente um endereço por passagem do loop. OTA e serviços de
  rede são atendidos antes de qualquer tentativa I²C.
- Os relés não são restaurados ligados após reboot: o padrão industrial adotado
  nesta fase é sempre iniciar todos desligados.

## Segurança e instalação industrial

Nesta primeira versão, REST, Modbus e OTA não possuem autenticação e devem ficar
em VLAN/rede industrial confiável, protegidos por firewall. Não exponha as portas
80, 3232 (OTA), 502 ou 1883 diretamente à internet. MQTT aceita usuário/senha,
mas ainda não usa TLS.

Antes de energizar cargas reais:

1. confirme em bancada que `HIGH` realmente desliga todos os 16 canais;
2. teste a ordem P0→relé 1 até P15→relé 16;
3. use alimentação adequada, opto-isolação e proteção contra surtos;
4. mantenha I²C curto e separado de cabos de potência;
5. valide perda/retorno do cabo, broker fora do ar, múltiplos comandos e OTA;
6. defina intertravamentos externos para cargas que não podem ligar juntas.

## Diagnóstico de boot e Ethernet da ETH01

O log `boot:0x3 (DOWNLOAD_BOOT...)` seguido por `waiting for download` não é
falha de DHCP nem do firmware: significa que GPIO0 foi amostrado em LOW no reset.
Na WT32-ETH01 o GPIO0 também recebe o clock RMII externo de 50 MHz e o GPIO16
habilita esse oscilador. Se o problema for intermitente, verifique a alimentação,
o circuito de enable do oscilador e se GPIO16 possui polarização definida durante
o boot. Nenhum outro periférico deve usar GPIO0 ou GPIO16.

Uma solução de hardware normalmente usada é manter o enable do oscilador em LOW
durante o reset e permitir que `ETH.begin()` o coloque em HIGH depois do boot. Só
adicione um pull-down externo após conferir o esquema exato da revisão da placa.

Eventos repetidos `cabo conectado`/`cabo desconectado` indicam perda do link
físico ou do clock RMII. Teste fonte, cabo, porta do switch e tensão/clock do PHY.
Depois que o link ficou estável, DHCP levou aproximadamente um segundo no log de
referência; portanto o atraso anterior ocorreu antes do DHCP.

## Organização do código

- `relay_switch_16ch.ino`: boot, Ethernet e OTA.
- `RelayController.*`: única camada que escreve no PCF8575.
- `AppConfig.*`: validação e persistência NVS.
- `NetworkServices.*`: REST, MQTT, health e Modbus TCP.
- `docs/API.md`: contratos e exemplos de integração.

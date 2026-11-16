# API — Relay Switch 5CH + 2DI

Base: `http://IP_DO_ESP/api/v1`. JSON UTF-8, sem autenticação; uso em rede confiável.

Esta documentação também está disponível no próprio dispositivo em `/api/docs`,
pelo link no painel inicial. Funciona sem internet. Os exemplos abaixo usam o
endereço do dispositivo que você está acessando quando abertos nessa página.

| Método | Rota | Função |
|---|---|---|
| GET | `/health` | Ethernet, MQTT, Modbus, Wi-Fi/NAT, entradas e webhook |
| GET | `/relays` | Cinco saídas comandadas e diagnóstico I²C |
| PUT | `/relays/1` … `/relays/5` | `{"on":true}` / `{"on":false}` |
| PUT | `/relays` | `{"mask":31}` ou `{"states":[true,false,false,true,false]}` |
| GET | `/inputs` | Estado das duas entradas, sem alterar saídas |
| GET | `/config` | Configuração com segredos mascarados |
| PUT | `/config` | Atualização parcial persistente |
| GET | `/i2c` | Diagnóstico em cache |
| POST | `/i2c/scan` | Scan incremental; resposta 202 |
| POST | `/reboot` | Reinício após 500 ms; resposta 202 |

Máscaras de saída: bit 0 = relé 1, bit 4 = relé 5, valores 0–31. Entradas: bit 0 = DI1/P5, bit 1 = DI2/P6, valores 0–3.

## Exemplos de requisições

Exemplos para terminal com curl (sintaxe de shell Linux/macOS):

```sh
# Consultar as duas entradas
curl http://IP_DO_ESP/api/v1/inputs

# Ligar somente o relé 1
curl -X PUT http://IP_DO_ESP/api/v1/relays/1 -H 'Content-Type: application/json' -d '{"on":true}'

# Desligar somente o relé 1
curl -X PUT http://IP_DO_ESP/api/v1/relays/1 -H 'Content-Type: application/json' -d '{"on":false}'

# Desligar os cinco relés
curl -X PUT http://IP_DO_ESP/api/v1/relays -H 'Content-Type: application/json' -d '{"mask":0}'

# Alterar somente o debounce; reiniciar para aplicar
curl -X PUT http://IP_DO_ESP/api/v1/config -H 'Content-Type: application/json' -d '{"io":{"debounce_ms":80}}'
```

GET faz consultas. Comandos dos relés usam PUT com JSON e `Content-Type: application/json`.

`relays.outputs_enabled` e `health.relay_controller.outputs_enabled` informam a
habilitação física GPIO4. Ela exige circuito EN ativo em HIGH na PCB e pull-down
externo. Sem essa ligação, o bloqueio do firmware não impede fisicamente os relés.

## Consultar entradas

```json
{
  "ok": true,
  "inputs": {
    "valid": true,
    "mask": 1,
    "sequence": 7,
    "sample_ms": 50200,
    "channels": [
      {"channel": 1, "active": true, "changed_ms": 50150},
      {"channel": 2, "active": false, "changed_ms": 0}
    ]
  }
}
```

503 e `valid:false` indicam leitura inválida; não trate o cache como estado atual. Tempos em ms desde o boot, com wrap de 32 bits. Sequência incrementa por canal alterado e reinicia no boot.

## Configurar

PUT `/config`, campos ausentes preservados:

```json
{
  "device_name": "reles-painel-01",
  "network": {
    "dhcp": true,
    "ip": "192.168.1.50",
    "gateway": "192.168.1.1",
    "subnet": "255.255.255.0",
    "dns1": "192.168.1.1",
    "dns2": "8.8.8.8"
  },
  "io": {"address": 32, "input_active_low": true, "debounce_ms": 50},
  "mqtt": {
    "enabled": false,
    "host": "192.168.1.10",
    "port": 1883,
    "user": "",
    "password": "",
    "base_topic": "industrial/relay5",
    "keep_alive": 30
  },
  "webhook": {
    "enabled": true,
    "url": "http://192.168.1.10/api/entradas",
    "token": "token-do-servidor",
    "ca_cert": ""
  },
  "wifi_ap": {
    "enabled": true,
    "ssid": "Display-Painel",
    "password": "troque-esta-senha",
    "ip": "192.168.4.1",
    "dns": "192.168.1.1",
    "channel": 6
  },
  "modbus": {"enabled": false, "port": 502, "unit_id": 1}
}
```

Retorno: `ok`, `saved`, `reboot_required`, `config` com segredos mascarados. `********` preserva o segredo; string vazia apaga. HTTPS exige CA PEM. MQTT/webhook aplicam sem reboot; nome, Ethernet, Wi-Fi e I/O exigem reinício. O sinalizador indica mudanças nessa requisição, sem registrar todas as alterações pendentes de reinício. Endereço decimal: 32–39 para PCF8574, 56–63 para PCF8574A.

## Webhook

POST JSON com `X-Event-ID` e, se configurado, `Authorization: Bearer TOKEN`:

```json
{
  "event": "input.changed",
  "device_name": "reles-painel-01",
  "event_id": "reles-painel-01-a1b2c3d4-7-1",
  "sequence": 7,
  "channel": 1,
  "active": true,
  "mask": 1,
  "changed_ms": 50150
}
```

ID inclui valor aleatório por boot, sequência e canal. Duas mudanças na mesma amostra podem compartilhar a sequência final, mas têm IDs diferentes. Retorne 2xx e deduplique por ID. Não há evento inicial no boot. Até três tentativas; 4xx terminais exceto 408/429. Fila volátil de oito eventos: perda possível em reinício, fila cheia ou falhas esgotadas. Eventos enfileirados mantêm destino/credenciais originais.

`health.webhook.last_http_status`: HTTP positivo ou erro negativo HTTPClient; `-1000` sem Ethernet, `-1001` sem horário válido para TLS, `-1002` falha ao inicializar URL. Contadores `delivered`, `failed`, `dropped` desde o boot.

## Erros

JSON malformado: 400 (configuração: 422). Canal, máscara ou valores inválidos: 422. Falha PCF8574: 503; a máscara comandada permanece em RAM e é reaplicada na recuperação. Não confirma os contatos físicos.

## MQTT e Modbus

Prefixo MQTT padrão: `industrial/relay5/relay-switch-5ch`. Se o nome do dispositivo
ou tópico base mudar, o prefixo muda também. MQTT precisa ser habilitado na configuração.

| Tópico relativo ao prefixo | Direção e conteúdo |
|---|---|
| `relay/1/set` … `relay/5/set` | Comando: ON/OFF ou JSON `{"on":true}` |
| `relays/set` | Comando: `{"mask":0}` (0–31) ou cinco `states` |
| `relay/1/state` … `relay/5/state` | Estado comandado ON/OFF, retido |
| `relays/state` | Cinco saídas e diagnóstico I²C, JSON retido |
| `inputs/state` | Entradas, validade, máscara e sequência, JSON retido |
| `availability` | online / LWT offline, retido |
| `health` | Diagnóstico a cada 30 segundos |
| `config/set` | Atualização parcial com o mesmo JSON de PUT /config |
| `config/get` | Solicita publicação da configuração; payload ignorado |
| `config/status` | Resultado/configuração com segredos mascarados, retido |

O estado atual das entradas é publicado após mudanças e reconexão MQTT; não há
histórico das bordas durante desconexão. Modbus TCP opcional: coils `0..4` para
relés 1–5. Unit ID é informativo; a biblioteca aceita qualquer Unit ID.

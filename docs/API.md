# Contratos REST e MQTT

Substitua `192.168.1.50` pelo IP mostrado no monitor serial ou pelo IP fixo
configurado.

## Health check

```bash
curl http://192.168.1.50/api/v1/health
```

Resposta resumida:

```json
{
  "ok": true,
  "health": {
    "status": "ok",
    "device_name": "relay-switch-16ch",
    "uptime_ms": 125400,
    "free_heap": 251840,
    "min_free_heap": 241120,
    "ethernet": {
      "link": true,
      "ip": "192.168.1.50",
      "mac": "AA:BB:CC:DD:EE:FF",
      "speed_mbps": 100,
      "full_duplex": true,
      "dhcp": true
    },
    "relay_controller": {
      "pcf8575_ok": true,
      "mask": 0,
      "write_errors": 0
    },
    "mqtt": {
      "enabled": false,
      "connected": false,
      "state": -1,
      "reconnects": 0,
      "messages_received": 0
    },
    "modbus": {
      "enabled": false,
      "server_started": false,
      "configured_port": 502,
      "active_port": 0,
      "unit_id": 1
    }
  }
}
```

`status: degraded` indica falha na comunicação I²C com o PCF8575.

## Ler todos os relés

```bash
curl http://192.168.1.50/api/v1/relays
```

```json
{
  "ok": true,
  "relays": {
    "mask": 5,
    "pcf8575_ok": true,
    "write_errors": 0,
    "channels": [
      {"channel": 1, "on": true},
      {"channel": 2, "on": false},
      {"channel": 3, "on": true}
    ]
  }
}
```

O array real sempre contém os 16 canais. Na máscara, bit 0 é relé 1 e bit 15
é relé 16.

## Acionar um relé

```bash
curl -X PUT http://192.168.1.50/api/v1/relays/4 \
  -H "Content-Type: application/json" \
  -d '{"on":true}'
```

```json
{
  "ok": true,
  "channel": 4,
  "on": true,
  "mask": 8
}
```

Para desligar, envie `{"on":false}`.

## Acionar vários relés

Relés 1 e 3 ligados (`1 + 4 = 5`):

```bash
curl -X PUT http://192.168.1.50/api/v1/relays \
  -H "Content-Type: application/json" \
  -d '{"mask":5}'
```

Também é possível enviar exatamente 16 estados:

```json
{
  "states": [
    true, false, true, false,
    false, false, false, false,
    false, false, false, false,
    false, false, false, false
  ]
}
```

## Consultar configuração

```bash
curl http://192.168.1.50/api/v1/config
```

```json
{
  "ok": true,
  "config": {
    "device_name": "relay-switch-16ch",
    "network": {
      "dhcp": true,
      "ip": "192.168.1.50",
      "gateway": "192.168.1.1",
      "subnet": "255.255.255.0",
      "dns1": "192.168.1.1",
      "dns2": "8.8.8.8"
    },
    "mqtt": {
      "enabled": false,
      "host": "",
      "port": 1883,
      "user": "",
      "password": "",
      "base_topic": "industrial/relay16",
      "keep_alive": 30
    },
    "modbus": {
      "enabled": false,
      "port": 502,
      "unit_id": 1
    }
  }
}
```

Quando existe senha MQTT gravada, o GET retorna `********`, nunca a senha.

## Configurar MQTT

A atualização é parcial: campos omitidos permanecem como estavam.

```bash
curl -X PUT http://192.168.1.50/api/v1/config \
  -H "Content-Type: application/json" \
  -d '{
    "device_name":"relay-painel-01",
    "mqtt":{
      "enabled":true,
      "host":"192.168.1.10",
      "port":1883,
      "user":"relay01",
      "password":"trocar-esta-senha",
      "base_topic":"fabrica/setor-a/relays",
      "keep_alive":30
    }
  }'
```

```json
{
  "ok": true,
  "saved": true,
  "reboot_required": true,
  "config": {
    "device_name": "relay-painel-01"
  }
}
```

MQTT é reconfigurado sem reiniciar. A resposta pede reboot porque a alteração do
`device_name` só muda completamente hostname e OTA depois da reinicialização.

## Configurar IP estático

Faça esta chamada enquanto ainda conhece o endereço DHCP atual:

```bash
curl -X PUT http://192.168.1.50/api/v1/config \
  -H "Content-Type: application/json" \
  -d '{
    "network":{
      "dhcp":false,
      "ip":"192.168.10.50",
      "gateway":"192.168.10.1",
      "subnet":"255.255.255.0",
      "dns1":"192.168.10.1",
      "dns2":"8.8.8.8"
    }
  }'
```

Resposta: `reboot_required: true`. Em seguida:

```bash
curl -X POST http://192.168.1.50/api/v1/reboot
```

Após o reboot, use `192.168.10.50`. Para voltar ao DHCP, envie
`{"network":{"dhcp":true}}` e reinicie.

## Habilitar Modbus TCP

```bash
curl -X PUT http://192.168.1.50/api/v1/config \
  -H "Content-Type: application/json" \
  -d '{"modbus":{"enabled":true,"port":502,"unit_id":1}}'
```

Habilitar na porta já configurada começa o atendimento em tempo de execução.
Trocar a porta retorna `reboot_required: true`.

Exemplo com `mbpoll`, ligando o relé 1 (coil 00001):

```bash
mbpoll -m tcp -a 1 -r 1 -t 0 -1 192.168.1.50 1
```

Leia 16 coils conforme a sintaxe da ferramenta Modbus usada. Algumas ferramentas
mostram endereços começando em 1, embora o valor enviado no protocolo comece em 0.

## Diagnosticar o I²C

Este GET é imediato: não transmite no barramento e não atrasa Ethernet/OTA.

```bash
curl http://192.168.1.50/api/v1/i2c
```

```json
{
  "ok": true,
  "note": "leitura instantanea do cache; este GET nao acessa nem bloqueia o I2C",
  "i2c": {
    "bus": {
      "sda_gpio": 33,
      "scl_gpio": 32,
      "frequency_hz": 100000,
      "timeout_ms": 5,
      "sda_level": "HIGH",
      "scl_level": "HIGH",
      "stuck_low": false
    },
    "expected_device": {
      "name": "PCF8575",
      "address": "0x20",
      "ack": false,
      "last_error_code": 2,
      "last_error": "NACK no endereco",
      "last_transaction_us": 210,
      "write_errors": 8
    },
    "scan": {
      "running": false,
      "completed": false,
      "current_address": 0,
      "duration_ms": 0,
      "nack_count": 0,
      "timeout_count": 0,
      "other_error_count": 0,
      "found_addresses": []
    },
    "diagnosis": "PCF8575 sem ACK; execute POST /api/v1/i2c/scan"
  }
}
```

Inicie a busca completa:

```bash
curl -X POST http://192.168.1.50/api/v1/i2c/scan
```

Em seguida consulte novamente `GET /api/v1/i2c`. O firmware verifica um endereço
por passagem do loop; assim, mesmo com SDA/SCL defeituosos, uma única tentativa
fica limitada a 5 ms e os serviços Ethernet continuam prioritários.

Interpretação:

- `SDA` ou `SCL` em LOW: curto, dispositivo prendendo a linha ou pull-up errado;
- 126 NACKs e nenhum endereço: barramento livre, mas nenhum dispositivo alimentado
  e endereçado respondeu;
- `0x21`…`0x27` encontrado: A0/A1/A2 não estão na condição esperada para `0x20`;
- timeouts: verifique nível lógico, pull-ups, solda, ruído e alimentação;
- `0x20` encontrado mas escrita falha: conexão intermitente ou PCF8575 defeituoso.

## MQTT: comandos e respostas

Com a configuração padrão e device name padrão:

```bash
mosquitto_sub -h 192.168.1.10 -t 'industrial/relay16/relay-switch-16ch/#' -v
```

Ligar relé 4:

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'industrial/relay16/relay-switch-16ch/relay/4/set' \
  -m 'ON' -q 1
```

Ou:

```json
{"on": true}
```

Ligar relés 1 e 3 e desligar os demais:

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'industrial/relay16/relay-switch-16ch/relays/set' \
  -m '{"mask":5}' -q 1
```

O dispositivo publica, de forma retida:

```text
industrial/relay16/relay-switch-16ch/relay/1/state ON
industrial/relay16/relay-switch-16ch/relay/2/state OFF
industrial/relay16/relay-switch-16ch/relays/state {"mask":5,...}
```

Configurar por MQTT usa o mesmo objeto parcial da API:

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'industrial/relay16/relay-switch-16ch/config/set' \
  -m '{"modbus":{"enabled":true,"port":502,"unit_id":1}}' -q 1
```

O resultado aparece em `config/status`, sempre com a senha mascarada.

## Erros REST

Formato padrão:

```json
{
  "ok": false,
  "error": "canal deve estar entre 1 e 16"
}
```

- `400`: JSON malformado;
- `404`: rota inexistente;
- `422`: campo ou valor inválido;
- `503`: comando aceito logicamente, porém o PCF8575 não respondeu no I²C.

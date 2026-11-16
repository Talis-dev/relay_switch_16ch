#pragma once
#include <Arduino.h>

// Gerado por node scripts/build-api-docs.cjs; fonte: docs/API.md.
static const char API_DOCS_PAGE[] PROGMEM = R"APIDOC(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Documentação da API — Relay Switch 5CH</title><style>
:root{color-scheme:dark;font:16px system-ui,sans-serif;color:#e7edf5;background:#101824}*{box-sizing:border-box}body{margin:0}main{max-width:1050px;margin:auto;padding:24px}nav{position:sticky;top:0;background:#101824;padding:16px 0;border-bottom:1px solid #34475c}a{color:#8ec4ff}h1{font-size:1.75rem}h2{font-size:1.25rem;margin-top:32px}p{line-height:1.6;color:#ced9e5}code{font-family:ui-monospace,monospace;font-size:.87em;overflow-wrap:anywhere;background:#1b2736;padding:2px 4px;border-radius:4px}pre{background:#1b2736;padding:16px;border:1px solid #34475c;border-radius:8px;overflow:auto;line-height:1.6}pre code{padding:0;white-space:pre;overflow-wrap:normal}.table-scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:.92rem}td,th{padding:12px;text-align:left;border:1px solid #34475c;vertical-align:top}th{background:#1b2736}@media(max-width:600px){main{padding:14px}td,th{padding:8px}h1{font-size:1.4rem}}
</style></head><body><main><nav><a href="/">← Voltar ao painel</a></nav>
<h1>API — Relay Switch 5CH + 2DI</h1>
<p>Base: <code>http://IP_DO_ESP/api/v1</code>. JSON UTF-8, sem autenticação; uso em rede confiável.</p>
<p>Esta documentação também está disponível no próprio dispositivo em <code>/api/docs</code>, pelo link no painel inicial. Funciona sem internet. Os exemplos abaixo usam o endereço do dispositivo que você está acessando quando abertos nessa página.</p>
<div class="table-scroll"><table><thead><tr><th>Método</th><th>Rota</th><th>Função</th></tr></thead><tbody><tr><td>GET</td><td><code>/health</code></td><td>Ethernet, MQTT, Modbus, Wi-Fi/NAT, entradas e webhook</td></tr><tr><td>GET</td><td><code>/relays</code></td><td>Cinco saídas comandadas e diagnóstico I²C</td></tr><tr><td>PUT</td><td><code>/relays/1</code> … <code>/relays/5</code></td><td><code>{&quot;on&quot;:true}</code> / <code>{&quot;on&quot;:false}</code></td></tr><tr><td>PUT</td><td><code>/relays</code></td><td><code>{&quot;mask&quot;:31}</code> ou <code>{&quot;states&quot;:[true,false,false,true,false]}</code></td></tr><tr><td>GET</td><td><code>/inputs</code></td><td>Estado das duas entradas, sem alterar saídas</td></tr><tr><td>GET</td><td><code>/config</code></td><td>Configuração com segredos mascarados</td></tr><tr><td>PUT</td><td><code>/config</code></td><td>Atualização parcial persistente</td></tr><tr><td>GET</td><td><code>/i2c</code></td><td>Diagnóstico em cache</td></tr><tr><td>POST</td><td><code>/i2c/scan</code></td><td>Scan incremental; resposta 202</td></tr><tr><td>POST</td><td><code>/reboot</code></td><td>Reinício após 500 ms; resposta 202</td></tr></tbody></table></div>
<p>Máscaras de saída: bit 0 = relé 1, bit 4 = relé 5, valores 0–31. Entradas: bit 0 = DI1/P5, bit 1 = DI2/P6, valores 0–3.</p>
<h2>Exemplos de requisições</h2>
<p>Exemplos para terminal com curl (sintaxe de shell Linux/macOS):</p>
<pre><code># Consultar as duas entradas
curl http://IP_DO_ESP/api/v1/inputs

# Ligar somente o relé 1
curl -X PUT http://IP_DO_ESP/api/v1/relays/1 -H 'Content-Type: application/json' -d '{&quot;on&quot;:true}'

# Desligar somente o relé 1
curl -X PUT http://IP_DO_ESP/api/v1/relays/1 -H 'Content-Type: application/json' -d '{&quot;on&quot;:false}'

# Desligar os cinco relés
curl -X PUT http://IP_DO_ESP/api/v1/relays -H 'Content-Type: application/json' -d '{&quot;mask&quot;:0}'

# Alterar somente o debounce; reiniciar para aplicar
curl -X PUT http://IP_DO_ESP/api/v1/config -H 'Content-Type: application/json' -d '{&quot;io&quot;:{&quot;debounce_ms&quot;:80}}'</code></pre>
<p>GET faz consultas. Comandos dos relés usam PUT com JSON e <code>Content-Type: application/json</code>.</p>
<p><code>relays.outputs_enabled</code> e <code>health.relay_controller.outputs_enabled</code> informam a habilitação física GPIO4. Ela exige circuito EN ativo em HIGH na PCB e pull-down externo. Sem essa ligação, o bloqueio do firmware não impede fisicamente os relés.</p>
<h2>Consultar entradas</h2>
<pre><code>{
  &quot;ok&quot;: true,
  &quot;inputs&quot;: {
    &quot;valid&quot;: true,
    &quot;mask&quot;: 1,
    &quot;sequence&quot;: 7,
    &quot;sample_ms&quot;: 50200,
    &quot;channels&quot;: [
      {&quot;channel&quot;: 1, &quot;active&quot;: true, &quot;changed_ms&quot;: 50150},
      {&quot;channel&quot;: 2, &quot;active&quot;: false, &quot;changed_ms&quot;: 0}
    ]
  }
}</code></pre>
<p>503 e <code>valid:false</code> indicam leitura inválida; não trate o cache como estado atual. Tempos em ms desde o boot, com wrap de 32 bits. Sequência incrementa por canal alterado e reinicia no boot.</p>
<h2>Configurar</h2>
<p>PUT <code>/config</code>, campos ausentes preservados:</p>
<pre><code>{
  &quot;device_name&quot;: &quot;reles-painel-01&quot;,
  &quot;network&quot;: {
    &quot;dhcp&quot;: true,
    &quot;ip&quot;: &quot;192.168.1.50&quot;,
    &quot;gateway&quot;: &quot;192.168.1.1&quot;,
    &quot;subnet&quot;: &quot;255.255.255.0&quot;,
    &quot;dns1&quot;: &quot;192.168.1.1&quot;,
    &quot;dns2&quot;: &quot;8.8.8.8&quot;
  },
  &quot;io&quot;: {&quot;address&quot;: 32, &quot;input_active_low&quot;: true, &quot;debounce_ms&quot;: 50},
  &quot;mqtt&quot;: {
    &quot;enabled&quot;: false,
    &quot;host&quot;: &quot;192.168.1.10&quot;,
    &quot;port&quot;: 1883,
    &quot;user&quot;: &quot;&quot;,
    &quot;password&quot;: &quot;&quot;,
    &quot;base_topic&quot;: &quot;industrial/relay5&quot;,
    &quot;keep_alive&quot;: 30
  },
  &quot;webhook&quot;: {
    &quot;enabled&quot;: true,
    &quot;url&quot;: &quot;http://192.168.1.10/api/entradas&quot;,
    &quot;token&quot;: &quot;token-do-servidor&quot;,
    &quot;ca_cert&quot;: &quot;&quot;
  },
  &quot;wifi_ap&quot;: {
    &quot;enabled&quot;: true,
    &quot;ssid&quot;: &quot;Display-Painel&quot;,
    &quot;password&quot;: &quot;troque-esta-senha&quot;,
    &quot;ip&quot;: &quot;192.168.4.1&quot;,
    &quot;dns&quot;: &quot;192.168.1.1&quot;,
    &quot;channel&quot;: 6
  },
  &quot;modbus&quot;: {&quot;enabled&quot;: false, &quot;port&quot;: 502, &quot;unit_id&quot;: 1}
}</code></pre>
<p>Retorno: <code>ok</code>, <code>saved</code>, <code>reboot_required</code>, <code>config</code> com segredos mascarados. <code>********</code> preserva o segredo; string vazia apaga. HTTPS exige CA PEM. MQTT/webhook aplicam sem reboot; nome, Ethernet, Wi-Fi e I/O exigem reinício. O sinalizador indica mudanças nessa requisição, sem registrar todas as alterações pendentes de reinício. Endereço decimal: 32–39 para PCF8574, 56–63 para PCF8574A.</p>
<h2>Webhook</h2>
<p>POST JSON com <code>X-Event-ID</code> e, se configurado, <code>Authorization: Bearer TOKEN</code>:</p>
<pre><code>{
  &quot;event&quot;: &quot;input.changed&quot;,
  &quot;device_name&quot;: &quot;reles-painel-01&quot;,
  &quot;event_id&quot;: &quot;reles-painel-01-a1b2c3d4-7-1&quot;,
  &quot;sequence&quot;: 7,
  &quot;channel&quot;: 1,
  &quot;active&quot;: true,
  &quot;mask&quot;: 1,
  &quot;changed_ms&quot;: 50150
}</code></pre>
<p>ID inclui valor aleatório por boot, sequência e canal. Duas mudanças na mesma amostra podem compartilhar a sequência final, mas têm IDs diferentes. Retorne 2xx e deduplique por ID. Não há evento inicial no boot. Até três tentativas; 4xx terminais exceto 408/429. Fila volátil de oito eventos: perda possível em reinício, fila cheia ou falhas esgotadas. Eventos enfileirados mantêm destino/credenciais originais.</p>
<p><code>health.webhook.last_http_status</code>: HTTP positivo ou erro negativo HTTPClient; <code>-1000</code> sem Ethernet, <code>-1001</code> sem horário válido para TLS, <code>-1002</code> falha ao inicializar URL. Contadores <code>delivered</code>, <code>failed</code>, <code>dropped</code> desde o boot.</p>
<h2>Erros</h2>
<p>JSON malformado: 400 (configuração: 422). Canal, máscara ou valores inválidos: 422. Falha PCF8574: 503; a máscara comandada permanece em RAM e é reaplicada na recuperação. Não confirma os contatos físicos.</p>
<h2>MQTT e Modbus</h2>
<p>Prefixo MQTT padrão: <code>industrial/relay5/relay-switch-5ch</code>. Se o nome do dispositivo ou tópico base mudar, o prefixo muda também. MQTT precisa ser habilitado na configuração.</p>
<div class="table-scroll"><table><thead><tr><th>Tópico relativo ao prefixo</th><th>Direção e conteúdo</th></tr></thead><tbody><tr><td><code>relay/1/set</code> … <code>relay/5/set</code></td><td>Comando: ON/OFF ou JSON <code>{&quot;on&quot;:true}</code></td></tr><tr><td><code>relays/set</code></td><td>Comando: <code>{&quot;mask&quot;:0}</code> (0–31) ou cinco <code>states</code></td></tr><tr><td><code>relay/1/state</code> … <code>relay/5/state</code></td><td>Estado comandado ON/OFF, retido</td></tr><tr><td><code>relays/state</code></td><td>Cinco saídas e diagnóstico I²C, JSON retido</td></tr><tr><td><code>inputs/state</code></td><td>Entradas, validade, máscara e sequência, JSON retido</td></tr><tr><td><code>availability</code></td><td>online / LWT offline, retido</td></tr><tr><td><code>health</code></td><td>Diagnóstico a cada 30 segundos</td></tr><tr><td><code>config/set</code></td><td>Atualização parcial com o mesmo JSON de PUT /config</td></tr><tr><td><code>config/get</code></td><td>Solicita publicação da configuração; payload ignorado</td></tr><tr><td><code>config/status</code></td><td>Resultado/configuração com segredos mascarados, retido</td></tr></tbody></table></div>
<p>O estado atual das entradas é publicado após mudanças e reconexão MQTT; não há histórico das bordas durante desconexão. Modbus TCP opcional: coils <code>0..4</code> para relés 1–5. Unit ID é informativo; a biblioteca aceita qualquer Unit ID.</p>
</main><script>document.querySelectorAll('code').forEach(element=>{element.textContent=element.textContent.replaceAll('http://IP_DO_ESP',location.origin)});</script></body></html>)APIDOC";

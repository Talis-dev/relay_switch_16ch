// Testa o JavaScript real do painel com DOM/API simulados, sem dependencias npm.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../WebPage.h'), 'utf8');
const script = source.match(/<script>([\s\S]*?)<\/script>/)[1];

async function setup() {
  const nodes = new Map();
  function element(tag, id) {
    return {tag, id, children: [], value: '', textContent: '', disabled: false,
      type: '', checked: false, append(...items) {
        this.children.push(...items);
        for (const item of items) if (item.id) nodes.set(item.id, item);
      }};
  }
  for (const match of source.matchAll(/<(\w+)[^>]*\bid="([^"]+)"[^>]*>/g)) {
    const node = element(match[1], match[2]);
    node.type = match[0].match(/\btype="([^"]+)"/)?.[1] || '';
    node.disabled = /\bdisabled\b/.test(match[0]);
    nodes.set(node.id, node);
  }
  const config = {
    device_name: 'teste-5ch', network: {dhcp:true,ip:'10.1.0.50',gateway:'10.1.0.1',subnet:'255.255.255.0',dns1:'10.1.0.1',dns2:'8.8.8.8'},
    io:{address:32,debounce_ms:50,input_active_low:true},
    mqtt:{enabled:false,host:'broker',port:1883,user:'',password:'********',base_topic:'industrial/relay5',keep_alive:30},
    webhook:{enabled:false,url:'',token:'********',ca_cert:'********'},
    wifi_ap:{enabled:false,ssid:'Display',password:'********',ip:'192.168.4.1',dns:'8.8.8.8',channel:6},
    modbus:{enabled:false,port:502,unit_id:1}
  };
  const state = {online:true,ioValid:true,writeFails:false,channels:[false,false,false,false,false],requests:[]};
  const context = vm.createContext({document:{getElementById:id=>nodes.get(id),createElement:tag=>element(tag)},
    setInterval:()=>0,setTimeout:()=>0,confirm:()=>true,console,
    fetch:async(url, options)=>{
      if(!state.online) throw Error('offline');
      const route = url.replace('/api/v1/','');
      const body = options.body ? JSON.parse(options.body) : undefined;
      state.requests.push({route,method:options.method,body});
      let result, status = 200;
      if(route==='config') {if(body)Object.assign(config,body); result={config,ok:true,reboot_required:true};}
      else if(route==='health') result={health:{device_name:config.device_name,status:state.ioValid?'ok':'degraded',ethernet:{ip:'10.1.0.50',link:true},mqtt:{connected:false},wifi_ap:{started:true,ip:'192.168.4.1',clients:1,nat_enabled:true},webhook:{delivered:3,failed:1,dropped:0},inputs:{valid:state.ioValid,sequence:2,sample_ms:100,channels:[{channel:1,active:true},{channel:2,active:false}]}}};
      else if(route==='relays') {if(body?.mask===0)state.channels.fill(false);result={ok:state.ioValid,relays:{channels:state.channels.map((on,i)=>({channel:i+1,on}))}};}
      else if(route.startsWith('relays/')) {const i=Number(route.split('/')[1])-1;if(state.writeFails){status=503;result={ok:false,warning:'PCF8574 sem resposta I2C'}}else{state.channels[i]=body.on;result={ok:true};}}
      else if(route==='i2c') result={i2c:{scan:{completed:true}}};
      else result={ok:true};
      return {ok:status<400,status,json:async()=>result};
    }});
  vm.runInContext(script,context);
  await new Promise(resolve=>setImmediate(resolve));
  return {nodes,state,context,config};
}

test('cinco controles, duas entradas e configuracao carregada com segredos mascarados',async()=>{
  const {nodes}=await setup();
  assert.equal(nodes.get('relays').children.length,5);
  assert.equal(nodes.get('inputs').children.length,2);
  assert.equal(nodes.get('input1').textContent,'Ativa');
  assert.equal(nodes.get('input2').textContent,'Inativa');
  assert.equal(nodes.get('save').disabled,false);
  assert.equal(nodes.get('mqtt_password').value,'********');
});
test('controle individual usa PUT com canal correto e atualiza estado',async()=>{
  const {nodes,state}=await setup();
  await nodes.get('relay3').onclick();
  assert.equal(state.channels[2],true);
  assert.match(nodes.get('relay3').textContent,/Ligado/);
  assert.deepEqual(state.requests.find(r=>r.route==='relays/3'),{route:'relays/3',method:'PUT',body:{on:true}});
});
test('formulario envia tipos numericos, booleanos e preserva segredos',async()=>{
  const {nodes,state}=await setup();
  nodes.get('wifi_ap_enabled').checked=true;
  nodes.get('io_debounce_ms').value='80';
  await nodes.get('settings').onsubmit({preventDefault(){}});
  const {body}=state.requests.find(r=>r.route==='config'&&r.method==='PUT');
  assert.equal(body.io.debounce_ms,80);
  assert.equal(body.wifi_ap.enabled,true);
  assert.equal(body.mqtt.password,'********');
  assert.equal(body.webhook.ca_cert,'********');
  assert.match(nodes.get('notice').textContent,/Reinicie/);
});
test('falha de comando e leitura invalida ficam visiveis',async()=>{
  const {nodes,state,context}=await setup();
  state.writeFails=true;
  await nodes.get('relay1').onclick();
  assert.match(nodes.get('notice').textContent,/PCF8574/);
  state.ioValid=false;
  await vm.runInContext('refresh()',context);
  assert.equal(nodes.get('input1').textContent,'Leitura inválida');
});
test('perda de comunicacao desabilita controles e invalida entradas exibidas',async()=>{
  const {nodes,state,context}=await setup();
  state.online=false;
  await vm.runInContext('refresh()',context);
  for(let i=1;i<=5;i++)assert.equal(nodes.get('relay'+i).disabled,true);
  assert.equal(nodes.get('input1').textContent,'Sem comunicação');
});

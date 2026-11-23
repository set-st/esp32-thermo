#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ThermoHub</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--mut:#6b7280;--acc:#2563eb;--bad:#dc2626;--ok:#16a34a;--bd:#e5e7eb}
@media(prefers-color-scheme:dark){:root{--bg:#111318;--card:#1b1e25;--fg:#e6e8ec;--mut:#9aa1ad;--bd:#2b2f38}}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,sans-serif;background:var(--bg);color:var(--fg)}
header{display:flex;align-items:center;gap:12px;padding:12px 16px;background:var(--card);border-bottom:1px solid var(--bd);flex-wrap:wrap}
header b{font-size:18px}#st{color:var(--mut);font-size:13px;margin-left:auto}
nav{display:flex;gap:4px;padding:8px 16px;overflow-x:auto}
nav button{border:0;background:none;color:var(--mut);padding:8px 12px;border-radius:8px;cursor:pointer;font:inherit}
nav button.on{background:var(--card);color:var(--fg);box-shadow:0 1px 2px #0002}
main{padding:8px 16px 32px;max-width:960px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;margin-bottom:12px}
.big{font-size:30px;font-weight:600}.mut{color:var(--mut);font-size:13px}.bad{color:var(--bad)}.ok{color:var(--ok)}
h3{margin:0 0 10px;font-size:15px}
label{display:block;margin:8px 0 2px;font-size:13px;color:var(--mut)}
input,select{width:100%;padding:7px 9px;border:1px solid var(--bd);border-radius:8px;background:var(--bg);color:var(--fg);font:inherit}
input[type=checkbox]{width:auto}
table{width:100%;border-collapse:collapse}td,th{padding:6px 4px;text-align:left;border-bottom:1px solid var(--bd);vertical-align:middle}
th{font-size:12px;color:var(--mut);font-weight:500}
.btn{border:0;background:var(--acc);color:#fff;padding:8px 14px;border-radius:8px;cursor:pointer;font:inherit;margin:10px 6px 0 0}
.btn.sec{background:var(--bd);color:var(--fg)}.btn.red{background:var(--bad)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:0 12px}
code{background:var(--bg);padding:1px 5px;border-radius:4px}
#toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#111;color:#fff;padding:10px 16px;border-radius:8px;display:none}
.hide{display:none}
.sw{display:flex;align-items:center;gap:10px;cursor:pointer;margin:0 0 12px;color:var(--fg);font-size:15px}
.sw input{display:none}.sw i{width:42px;height:24px;border-radius:12px;background:var(--bd);position:relative;transition:.2s;flex:none}
.sw i:after{content:'';position:absolute;top:3px;left:3px;width:18px;height:18px;border-radius:50%;background:#fff;transition:.2s}
.sw input:checked+i{background:#f59e0b}.sw input:checked+i:after{left:21px}
.badge{display:inline-block;background:#f59e0b;color:#000;font-size:11px;font-weight:600;padding:1px 6px;border-radius:6px;vertical-align:middle;margin-left:6px}
.lcdwrap{display:flex;flex-wrap:wrap;align-items:center;gap:16px}
.lcd{background:#1442c4;padding:12px 14px;border-radius:6px;border:8px solid #15171c;box-shadow:inset 0 0 14px #000a;max-width:100%;overflow-x:auto}
.lcd div{font:min(22px,4.3vw)/1.3 "Courier New",Consolas,monospace;font-weight:600;color:#e4ecff;white-space:pre;letter-spacing:.12em;text-shadow:0 0 5px #a8c4ff99}
.lcd.off{background:#2b3550}.lcd.off div{color:#8a94b0;text-shadow:none}
.enc{display:grid;grid-template-columns:repeat(3,auto);gap:6px}
.enc button{border:1px solid var(--bd);background:var(--bg);color:var(--fg);border-radius:10px;padding:10px 14px;font:inherit;font-size:18px;cursor:pointer;min-width:52px}
.enc button:active{background:var(--acc);color:#fff}
.enc .wide{grid-column:span 3;font-size:13px}
#nets{margin-top:6px}#nets button{display:flex;justify-content:space-between;width:100%;border:1px solid var(--bd);background:var(--bg);color:var(--fg);padding:8px 10px;border-radius:8px;margin-top:4px;cursor:pointer;font:inherit;text-align:left}
#nets button:hover{border-color:var(--acc)}#nets .mut{white-space:nowrap;margin-left:8px}
#demoBar{background:#f59e0b22;border:1px solid #f59e0b;border-radius:12px;padding:10px 14px;margin-bottom:12px}
</style></head><body>
<header><b>ThermoHub</b><span id="st">…</span></header>
<nav id="nav">
<button data-t="mon" class="on">Мониторинг</button><button data-t="ds">Термодатчики</button>
<button data-t="pins">Пины</button><button data-t="wifi">Wi‑Fi</button><button data-t="sys">Система</button>
</nav>
<main>

<section id="t-mon">
 <div id="demoBar" class="hide">⚠ Демо-режим: показания DS18B20 эмулируются. Выключить — вкладка «Термодатчики».</div>
 <div class="card"><h3>Дисплей <span class="mut" id="lcdInfo"></span></h3>
  <div class="lcdwrap">
   <div class="lcd" id="lcd"></div>
   <div class="enc">
    <button data-in='{"turn":-1}' title="Повернуть влево">⟲</button>
    <button data-in='{"press":"click"}' title="Клик">●</button>
    <button data-in='{"turn":1}' title="Повернуть вправо">⟳</button>
    <button class="wide" data-in='{"press":"long"}'>Долгое нажатие (меню коррекции)</button>
   </div>
  </div>
 </div>
 <div class="grid" id="monDs"></div>
 <div class="grid">
  <div class="card"><h3>AHT20</h3><div class="big" id="ahtT">—</div><div class="mut">влажность <span id="ahtH">—</span></div></div>
  <div class="card"><h3>BMP280</h3><div class="big" id="bmpP">—</div><div class="mut"><span id="bmpHpa">—</span> · <span id="bmpT">—</span></div></div>
 </div>
 <div class="card mut">API: <code>GET /api/sensors</code> (JSON) · <code>GET /metrics</code> (Prometheus) · <code>POST /api/offset {"slot":1,"offset":-0.3}</code></div>
</section>

<section id="t-ds" class="hide"><div class="card">
 <h3>Датчики DS18B20</h3>
 <label class="sw"><input type="checkbox" id="demo"><i></i>Демо-режим (эмуляция 4 датчиков, пока реальные не подключены)</label>
 <table><thead><tr><th>#</th><th>Имя</th><th>Адрес на шине</th><th>Коррекция, °C</th><th>Сырое → итог</th></tr></thead><tbody id="dsTbl"></tbody></table>
 <button class="btn sec" id="owScan">Сканировать шину</button><button class="btn" id="dsSave">Сохранить</button>
 <div class="mut" id="owRes"></div>
 <p class="mut">Новые датчики при загрузке сами занимают пустые слоты. Коррекцию можно менять и с энкодера: долгое нажатие → выбор T1..T4 → клик → крутить → клик (сохранить).</p>
</div></section>

<section id="t-pins" class="hide"><div class="card">
 <h3>Назначение GPIO</h3>
 <div class="row">
  <div><label>I2C SDA</label><select id="p_sda" data-out></select></div>
  <div><label>I2C SCL</label><select id="p_scl" data-out></select></div>
  <div><label>1‑Wire (DS18B20)</label><select id="p_onewire" data-out></select></div>
  <div></div>
  <div><label>Энкодер A (CLK)</label><select id="p_encA" data-opt></select></div>
  <div><label>Энкодер B (DT)</label><select id="p_encB" data-opt></select></div>
  <div><label>Кнопка энкодера (SW)</label><select id="p_encBtn" data-opt></select></div>
 </div>
 <p class="mut">⚠ — strapping‑пины (0, 2, 5, 12, 15): могут мешать загрузке. GPIO34–39 только вход и без подтяжки — для энкодера нужны внешние резисторы 10к на 3.3В. На 1‑Wire нужен резистор 4.7к на 3.3В.</p>
 <h3>Дисплей и энкодер</h3>
 <div class="row">
  <div><label>Адрес LCD (hex, 0 = авто)</label><input id="lcdAddr"></div>
  <div><label>Размер LCD</label><select id="lcdSize"><option value="20x4">20×4</option><option value="16x4">16×4</option><option value="16x2">16×2</option><option value="20x2">20×2</option></select></div>
  <div><label>Шагов энкодера на щелчок</label><select id="encSteps"><option>4</option><option>2</option><option>1</option></select></div>
  <div><label><input type="checkbox" id="encRev"> Инвертировать направление</label></div>
 </div>
 <button class="btn sec" id="i2cScan">Сканировать I2C</button><button class="btn" id="pinsSave">Сохранить и перезагрузить</button>
 <div class="mut" id="i2cRes"></div>
</div></section>

<section id="t-wifi" class="hide"><div class="card">
 <h3>Wi‑Fi</h3>
 <div id="wStat" class="mut"></div>
 <label>Сеть (SSID)</label><input id="ssid"><div id="nets"></div>
 <label>Пароль</label><input id="wpass" type="password" placeholder="(не менять)">
 <label>Имя хоста (mDNS: имя.local)</label><input id="hostname">
 <label>Пароль точки доступа (мин. 8 символов, пусто = открытая)</label><input id="appass" type="password" placeholder="(не менять)">
 <button class="btn sec" id="wScan">Найти сети</button><button class="btn" id="wSave">Сохранить и перезагрузить</button>
 <p class="mut">Если устройство не подключится к сети за 20 с, оно поднимет точку доступа <b id="apName"></b> (адрес 192.168.4.1). Чтобы включить её принудительно, держите кнопку энкодера при включении питания.</p>
</div></section>

<section id="t-sys" class="hide">
 <div class="card"><h3>Общие</h3>
  <div class="row">
   <div><label>Интервал опроса, мс</label><input id="poll" type="number" min="1000" max="60000" step="500"></div>
   <div><label>Давление на LCD</label><select id="punit"><option value="1">мм рт. ст.</option><option value="0">гПа</option></select></div>
  </div>
  <button class="btn" id="sysSave">Сохранить</button>
 </div>
 <div class="card"><h3>Обновление прошивки</h3>
  <input type="file" id="fw" accept=".bin"><button class="btn" id="fwBtn">Загрузить</button> <span id="fwSt" class="mut"></span>
 </div>
 <div class="card"><h3>Устройство</h3><div id="info" class="mut"></div>
  <button class="btn sec" id="reboot">Перезагрузить</button><button class="btn red" id="reset">Сброс настроек</button>
 </div>
</section>
</main>
<div id="toast"></div>
<script>
const $=id=>document.getElementById(id);
let C=null,I=null,S=null,owFound=[];
const dirty=new Set();
const PINS=[0,2,4,5,12,13,14,15,16,17,18,19,21,22,23,25,26,27,32,33,34,35,36,39];
const STRAP=[0,2,5,12,15];
function toast(t,ms=3000){const e=$('toast');e.textContent=t;e.style.display='block';clearTimeout(e._t);e._t=setTimeout(()=>e.style.display='none',ms)}
async function api(url,body,method){
  const o=body!==undefined?{method:method||'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{method:method||'GET'};
  const r=await fetch(url,o);let j={};try{j=await r.json()}catch(e){}
  if(!r.ok||j.ok===false)throw new Error(j.error||('HTTP '+r.status));return j}
const f1=v=>v==null?'—':v.toFixed(1);

document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('nav button').forEach(x=>x.classList.toggle('on',x==b));
  document.querySelectorAll('main section').forEach(s=>s.classList.toggle('hide',s.id!='t-'+b.dataset.t))});
['wpass','appass'].forEach(id=>$(id).addEventListener('input',()=>dirty.add(id)));

async function save(part,msg){
  try{const r=await api('/api/config',part);
    if(r.reboot){toast('Сохранено, перезагрузка…',9000);setTimeout(()=>location.reload(),9000)}
    else{toast(msg||'Сохранено');await loadCfg()}
  }catch(e){toast('Ошибка: '+e.message,5000)}}

function fillPins(){
  document.querySelectorAll('select[data-out],select[data-opt]').forEach(s=>{
    const out=s.hasAttribute('data-out');let h=out?'':'<option value="-1">не используется</option>';
    PINS.filter(p=>!out||p<34).forEach(p=>h+=`<option value="${p}">GPIO${p}${STRAP.includes(p)?' ⚠':''}${p>=34?' (вход)':''}</option>`);
    s.innerHTML=h})}

function renderDs(){
  let h='';
  C.ds.forEach((d,i)=>{
    const addrs=new Set([d.addr,...owFound.map(x=>x.addr)].filter(Boolean));
    let opt='<option value="">— пусто —</option>';addrs.forEach(a=>opt+=`<option ${a==d.addr?'selected':''}>${a}</option>`);
    h+=`<tr><td>T${i+1}</td><td><input id="dn${i}" value="${d.name.replace(/"/g,'&quot;')}" maxlength="23"></td>
    <td><select id="da${i}">${opt}</select></td>
    <td><input id="do${i}" type="number" step="0.1" min="-20" max="20" value="${d.offset}"></td><td id="dv${i}">—</td></tr>`});
  $('dsTbl').innerHTML=h;updDsLive()}
function updDsLive(){if(!S||!C)return;S.ds18b20.forEach((d,i)=>{const e=$('dv'+i);if(e)e.textContent=d.ok?`${f1(d.raw)} → ${f1(d.temp)}`:'нет связи'})}

async function loadCfg(){
  const r=await api('/api/config');C=r.config;I=r.info;
  renderDs();
  const p=C.pins;['sda','scl','onewire','encA','encB','encBtn'].forEach(k=>$('p_'+k).value=p[k]);
  $('lcdAddr').value=C.lcd.addr?C.lcd.addr.toString(16):'0';$('lcdSize').value=C.lcd.cols+'x'+C.lcd.rows;
  $('encSteps').value=C.encoder.stepsPerDetent;$('encRev').checked=C.encoder.reverse;
  $('ssid').value=C.wifi.ssid;$('hostname').value=C.wifi.hostname;$('apName').textContent=I.apSsid;
  $('wStat').innerHTML=I.staConnected?'<span class="ok">● Подключено к '+C.wifi.ssid+', IP '+I.ip+'</span>':(C.wifi.ssid?'<span class="bad">● Нет подключения к '+C.wifi.ssid+(I.staProblem?': '+I.staProblem:'')+'</span>':'Сеть не настроена');
  $('demo').checked=C.dsDemo;$('poll').value=C.pollMs;$('punit').value=C.pressUnit;
  ['wpass','appass'].forEach(id=>{$(id).value='';dirty.delete(id)});
  $('info').innerHTML=`Прошивка ${I.version} · MAC ${I.mac} · IP ${I.ip}<br>LCD: ${I.lcdAddr?'0x'+I.lcdAddr.toString(16):'не найден'} · BMP280: ${I.bmpAddr?'0x'+I.bmpAddr.toString(16):'не найден'} · RAM ${Math.round(I.freeHeap/1024)} КБ · аптайм ${Math.round(I.uptime_s/60)} мин`;
}

async function poll(){
  try{S=await api('/api/sensors');
    let h='';S.ds18b20.forEach(d=>h+=`<div class="card"><h3>${d.name}${d.demo?'<span class="badge">ДЕМО</span>':''}</h3><div class="big ${d.ok?'':'bad'}">${d.ok?f1(d.temp)+' °C':'нет связи'}</div><div class="mut">${d.demo?'эмуляция':(d.addr||'слот пуст')}${d.offset?` · корр. ${d.offset>0?'+':''}${d.offset}`:''}</div></div>`);
    $('monDs').innerHTML=h;$('demoBar').classList.toggle('hide',!S.demo);
    const a=S.aht20,b=S.bmp280;
    $('ahtT').textContent=a.ok?f1(a.temp)+' °C':'нет данных';$('ahtH').textContent=a.ok?f1(a.humidity)+' %':'—';
    $('bmpP').textContent=b.ok?f1(b.pressure_mmhg)+' мм':'нет данных';$('bmpHpa').textContent=b.ok?f1(b.pressure_hpa)+' гПа':'—';$('bmpT').textContent=b.ok?f1(b.temp)+' °C':'—';
    $('st').textContent=(S.wifi.connected?`${S.wifi.ip} · ${S.wifi.rssi} dBm`:`точка доступа ${S.wifi.ip}`)+` · ${Math.round(S.uptime_s/60)} мин`;
    updDsLive();
  }catch(e){$('st').textContent='нет связи с устройством'}}

$('demo').onchange=e=>save({dsDemo:e.target.checked},e.target.checked?'Демо-режим включён':'Демо-режим выключен');
$('owScan').onclick=async()=>{try{owFound=(await api('/api/scan/onewire')).found;
  $('owRes').textContent=owFound.length?'Найдено: '+owFound.map(x=>x.addr+(x.slot?` (T${x.slot})`:' (не назначен)')).join(', '):'Датчики не найдены';renderDs()}catch(e){toast(e.message)}};
$('dsSave').onclick=()=>{
  const ds=C.ds.map((_,i)=>({name:$('dn'+i).value,addr:$('da'+i).value,offset:parseFloat($('do'+i).value)||0}));
  const used=ds.map(d=>d.addr).filter(Boolean);if(new Set(used).size!=used.length)return toast('Один датчик назначен в два слота');
  save({ds})};
$('i2cScan').onclick=async()=>{try{const f=(await api('/api/scan/i2c')).found;
  const n={0x27:'LCD',0x3f:'LCD',0x38:'AHT20',0x76:'BMP280',0x77:'BMP280'};
  $('i2cRes').textContent=f.length?'Найдено: '+f.map(a=>'0x'+a.toString(16)+(n[a]?` (${n[a]})`:'')).join(', '):'Ничего не найдено'}catch(e){toast(e.message)}};
$('pinsSave').onclick=()=>{
  const pins={};['sda','scl','onewire','encA','encB','encBtn'].forEach(k=>pins[k]=parseInt($('p_'+k).value));
  const [cols,rows]=$('lcdSize').value.split('x').map(Number);const addr=parseInt($('lcdAddr').value||'0',16);
  if(isNaN(addr))return toast('Неверный адрес LCD');
  save({pins,lcd:{addr,cols,rows},encoder:{stepsPerDetent:+$('encSteps').value,reverse:$('encRev').checked}})};
function bars(r){return r>=-55?'▂▄▆█':r>=-67?'▂▄▆_':r>=-75?'▂▄__':'▂___'}
$('wScan').onclick=async()=>{const b=$('wScan');b.disabled=true;b.textContent='Поиск…';$('nets').innerHTML='';
  try{const n=(await api('/api/scan/wifi')).networks;
    if(!n.length)$('nets').innerHTML='<div class="mut">Сети не найдены</div>';
    n.forEach(x=>{const e=document.createElement('button');e.type='button';
      e.innerHTML=`<span></span><span class="mut">${bars(x.rssi)} ${x.rssi} dBm · кан. ${x.channel}${x.open?' · открытая':' · 🔒'}</span>`;
      e.firstChild.textContent=x.ssid;
      e.onclick=()=>{$('ssid').value=x.ssid;$('nets').innerHTML='';$('wpass').focus()};$('nets').appendChild(e)});
  }catch(e){toast('Сканирование: '+e.message,5000)}b.disabled=false;b.textContent='Найти сети'};
$('wSave').onclick=()=>{const w={ssid:$('ssid').value,hostname:$('hostname').value};
  if(dirty.has('wpass'))w.pass=$('wpass').value;if(dirty.has('appass'))w.apPass=$('appass').value;
  if(!w.hostname)return toast('Укажите имя хоста');save({wifi:w})};
$('sysSave').onclick=()=>save({pollMs:+$('poll').value,pressUnit:+$('punit').value});
$('reboot').onclick=async()=>{if(!confirm('Перезагрузить?'))return;await api('/api/reboot',{});toast('Перезагрузка…',8000);setTimeout(()=>location.reload(),8000)};
$('reset').onclick=async()=>{if(!confirm('Сбросить ВСЕ настройки, включая Wi‑Fi?'))return;await api('/api/factory-reset',{});toast('Сброшено. Подключитесь к точке доступа устройства.',10000)};
$('fwBtn').onclick=()=>{const f=$('fw').files[0];if(!f)return;const fd=new FormData();fd.append('fw',f);
  const x=new XMLHttpRequest();x.open('POST','/update');x.upload.onprogress=e=>$('fwSt').textContent=Math.round(e.loaded/e.total*100)+'%';
  x.onload=()=>{if(x.status==200){$('fwSt').textContent='Готово, перезагрузка…';setTimeout(()=>location.reload(),10000)}else $('fwSt').textContent='Ошибка: '+x.responseText};x.send(fd)};

function renderLcd(j){const e=$('lcd');e.innerHTML='';e.classList.toggle('off',!j.present);
  j.lines.forEach(t=>{const d=document.createElement('div');d.textContent=t.padEnd(j.cols,' ');e.appendChild(d)});
  $('lcdInfo').textContent=`${j.cols}×${j.rows}`+(j.present?'':' · физический дисплей не найден, показан эмулятор')}
let lcdBusy=false;
async function pollLcd(){if(lcdBusy||$('t-mon').classList.contains('hide'))return;lcdBusy=true;
  try{renderLcd(await api('/api/display'))}catch(e){}lcdBusy=false}
document.querySelectorAll('.enc button').forEach(b=>b.onclick=async()=>{
  try{lcdBusy=true;renderLcd(await api('/api/display/input',JSON.parse(b.dataset.in)))}catch(e){toast(e.message)}lcdBusy=false});
pollLcd();setInterval(pollLcd,800);
fillPins();loadCfg().catch(e=>toast(e.message));poll();setInterval(poll,2000);
</script></body></html>)HTML";

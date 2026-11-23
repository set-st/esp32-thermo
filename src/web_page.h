#pragma once
#include <Arduino.h>

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="uk"><head><meta charset="utf-8">
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
.lcd{border:8px solid #15171c;border-radius:6px;box-shadow:0 2px 6px #0004;line-height:0;max-width:100%;overflow:hidden;background:#1a44c8}
.lcd canvas{display:block;padding:10px}
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
<button data-t="mon" class="on">Моніторинг</button><button data-t="ds">Термодатчики</button>
<button data-t="pins">Піни</button><button data-t="wifi">Wi‑Fi</button><button data-t="sys">Система</button>
</nav>
<main>

<section id="t-mon">
 <div id="demoBar" class="hide">⚠ Демо-режим: показники DS18B20 емулюються. Вимкнути — вкладка «Термодатчики».</div>
 <div class="card"><h3>Дисплей <span class="mut" id="lcdInfo"></span></h3>
  <div class="lcdwrap" id="lcdwrap">
   <div class="lcd"><canvas id="lcd" role="img"></canvas></div>
   <div class="enc">
    <button data-in='{"turn":-1}' title="Повернути вліво">⟲</button>
    <button data-in='{"press":"click"}' title="Клік">●</button>
    <button data-in='{"turn":1}' title="Повернути вправо">⟳</button>
    <button class="wide" data-in='{"press":"long"}'>Довге натискання (меню корекції)</button>
   </div>
  </div>
 </div>
 <div class="grid" id="monDs"></div>
 <div class="grid">
  <div class="card"><h3>AHT20</h3><div class="big" id="ahtT">—</div><div class="mut">вологість <span id="ahtH">—</span></div></div>
  <div class="card"><h3>BMP280</h3><div class="big" id="bmpP">—</div><div class="mut"><span id="bmpHpa">—</span> · <span id="bmpT">—</span></div></div>
 </div>
 <div class="card mut">API: <code>GET /api/sensors</code> (JSON) · <code>GET /metrics</code> (Prometheus) · <code>POST /api/offset {"slot":1,"offset":-0.3}</code></div>
</section>

<section id="t-ds" class="hide"><div class="card">
 <h3>Датчики DS18B20</h3>
 <label class="sw"><input type="checkbox" id="demo"><i></i>Демо-режим (емуляція 4 датчиків, поки справжні не підключені)</label>
 <table><thead><tr><th>#</th><th>Ім'я</th><th>Адреса на шині</th><th>Корекція, °C</th><th>Сире → підсумок</th></tr></thead><tbody id="dsTbl"></tbody></table>
 <button class="btn sec" id="owScan">Сканувати шину</button><button class="btn" id="dsSave">Зберегти</button>
 <div class="mut" id="owRes"></div>
 <p class="mut">Нові датчики під час завантаження самі займають порожні слоти. Корекцію можна змінювати і з енкодера: довге натискання → вибір T1..T4 → клік → крутити → клік (зберегти).</p>
</div></section>

<section id="t-pins" class="hide"><div class="card">
 <h3>Призначення GPIO</h3>
 <div class="row">
  <div><label>I2C SDA</label><select id="p_sda" data-out></select></div>
  <div><label>I2C SCL</label><select id="p_scl" data-out></select></div>
  <div><label>1‑Wire (DS18B20)</label><select id="p_onewire" data-out></select></div>
  <div></div>
  <div><label>Енкодер A (CLK)</label><select id="p_encA" data-opt></select></div>
  <div><label>Енкодер B (DT)</label><select id="p_encB" data-opt></select></div>
  <div><label>Кнопка енкодера (SW)</label><select id="p_encBtn" data-opt></select></div>
 </div>
 <p class="mut">⚠ — strapping‑піни (0, 2, 5, 12, 15): можуть заважати завантаженню. GPIO34–39 лише вхід і без підтяжки — для енкодера потрібні зовнішні резистори 10к на 3.3В. На 1‑Wire потрібен резистор 4.7к на 3.3В.</p>
 <h3>Дисплей і енкодер</h3>
 <div class="row">
  <div><label>Адреса LCD (hex, 0 = авто)</label><input id="lcdAddr"></div>
  <div><label>Шрифт дисплея</label><select id="lcdCharset"><option value="0">Латиниця (транслітерація)</option><option value="1">Кирилиця (дисплей з кириличним ROM)</option></select></div>
  <div><label>Розмір LCD</label><select id="lcdSize"><option value="20x4">20×4</option><option value="16x4">16×4</option><option value="16x2">16×2</option><option value="20x2">20×2</option></select></div>
  <div><label>Кроків енкодера на клацання</label><select id="encSteps"><option>4</option><option>2</option><option>1</option></select></div>
  <div><label><input type="checkbox" id="encRev"> Інвертувати напрямок</label></div>
 </div>
 <button class="btn sec" id="i2cScan">Сканувати I2C</button><button class="btn" id="pinsSave">Зберегти і перезавантажити</button>
 <div class="mut" id="i2cRes"></div>
</div></section>

<section id="t-wifi" class="hide"><div class="card">
 <h3>Wi‑Fi</h3>
 <div id="wStat" class="mut"></div>
 <label>Мережа (SSID)</label><input id="ssid"><div id="nets"></div>
 <label>Пароль</label><input id="wpass" type="password" placeholder="(не змінювати)">
 <label>Ім'я хоста (mDNS: ім'я.local)</label><input id="hostname">
 <label>Пароль точки доступу (мін. 8 символів, порожньо = відкрита)</label><input id="appass" type="password" placeholder="(не змінювати)">
 <button class="btn sec" id="wScan">Знайти мережі</button><button class="btn" id="wSave">Зберегти і перезавантажити</button>
 <p class="mut">Якщо пристрій не підключиться до мережі за 20 с, він підніме точку доступу <b id="apName"></b> (адреса 192.168.4.1). Щоб увімкнути її примусово, тримайте кнопку енкодера під час увімкнення живлення.</p>
</div></section>

<section id="t-sys" class="hide">
 <div class="card"><h3>Загальні</h3>
  <div class="row">
   <div><label>Інтервал опитування, мс</label><input id="poll" type="number" min="1000" max="60000" step="500"></div>
   <div><label>Підсвітка дисплея</label><select id="blMode"><option value="0">Завжди увімкнена</option><option value="1">Вимикати, якщо немає дій</option><option value="2">Вимкнена</option></select></div>
   <div><label>Вимикати через, хв</label><input id="blMin" type="number" min="1" max="60" step="1"></div>
   <div><label>Тиск на LCD</label><select id="punit"><option value="1">мм рт. ст.</option><option value="0">гПа</option></select></div>
  </div>
  <button class="btn" id="sysSave">Зберегти</button>
 </div>
 <div class="card"><h3>Оновлення прошивки</h3>
  <input type="file" id="fw" accept=".bin"><button class="btn" id="fwBtn">Завантажити</button> <span id="fwSt" class="mut"></span>
 </div>
 <div class="card"><h3>Пристрій</h3><div id="info" class="mut"></div>
  <button class="btn sec" id="reboot">Перезавантажити</button><button class="btn red" id="reset">Скидання налаштувань</button>
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
    if(r.reboot){toast('Збережено, перезавантаження…',9000);setTimeout(()=>location.reload(),9000)}
    else{toast(msg||'Збережено');await loadCfg()}
  }catch(e){toast('Помилка: '+e.message,5000)}}

function fillPins(){
  document.querySelectorAll('select[data-out],select[data-opt]').forEach(s=>{
    const out=s.hasAttribute('data-out');let h=out?'':'<option value="-1">не використовується</option>';
    PINS.filter(p=>!out||p<34).forEach(p=>h+=`<option value="${p}">GPIO${p}${STRAP.includes(p)?' ⚠':''}${p>=34?' (вхід)':''}</option>`);
    s.innerHTML=h})}

function renderDs(){
  let h='';
  C.ds.forEach((d,i)=>{
    const addrs=new Set([d.addr,...owFound.map(x=>x.addr)].filter(Boolean));
    let opt='<option value="">— порожньо —</option>';addrs.forEach(a=>opt+=`<option ${a==d.addr?'selected':''}>${a}</option>`);
    h+=`<tr><td>T${i+1}</td><td><input id="dn${i}" value="${d.name.replace(/"/g,'&quot;')}" maxlength="30"></td>
    <td><select id="da${i}">${opt}</select></td>
    <td><input id="do${i}" type="number" step="0.1" min="-20" max="20" value="${d.offset}"></td><td id="dv${i}">—</td></tr>`});
  $('dsTbl').innerHTML=h;updDsLive()}
function updDsLive(){if(!S||!C)return;S.ds18b20.forEach((d,i)=>{const e=$('dv'+i);if(e)e.textContent=d.ok?`${f1(d.raw)} → ${f1(d.temp)}`:'немає зв\'язку'})}

async function loadCfg(){
  const r=await api('/api/config');C=r.config;I=r.info;
  renderDs();
  const p=C.pins;['sda','scl','onewire','encA','encB','encBtn'].forEach(k=>$('p_'+k).value=p[k]);
  $('lcdAddr').value=C.lcd.addr?C.lcd.addr.toString(16):'0';$('lcdSize').value=C.lcd.cols+'x'+C.lcd.rows;
  $('lcdCharset').value=C.lcd.charset;$('encSteps').value=C.encoder.stepsPerDetent;$('encRev').checked=C.encoder.reverse;
  $('ssid').value=C.wifi.ssid;$('hostname').value=C.wifi.hostname;$('apName').textContent=I.apSsid;
  $('wStat').innerHTML=I.staConnected?'<span class="ok">● Підключено до '+C.wifi.ssid+', IP '+I.ip+'</span>':(C.wifi.ssid?'<span class="bad">● Немає підключення до '+C.wifi.ssid+(I.staProblem?': '+I.staProblem:'')+'</span>':'Мережу не налаштовано');
  $('demo').checked=C.dsDemo;$('poll').value=C.pollMs;$('punit').value=C.pressUnit;$('blMode').value=C.lcd.blMode;$('blMin').value=Math.max(1,Math.round(C.lcd.blTimeout/60));blUi();
  ['wpass','appass'].forEach(id=>{$(id).value='';dirty.delete(id)});
  $('info').innerHTML=`Прошивка ${I.version} · MAC ${I.mac} · IP ${I.ip}<br>LCD: ${I.lcdAddr?'0x'+I.lcdAddr.toString(16):'не знайдено'} · BMP280: ${I.bmpAddr?'0x'+I.bmpAddr.toString(16):'не знайдено'} · RAM ${Math.round(I.freeHeap/1024)} КБ · аптайм ${Math.round(I.uptime_s/60)} хв`;
}

async function poll(){
  try{S=await api('/api/sensors');
    let h='';S.ds18b20.forEach(d=>h+=`<div class="card"><h3>${d.name}${d.demo?'<span class="badge">ДЕМО</span>':''}</h3><div class="big ${d.ok?'':'bad'}">${d.ok?f1(d.temp)+' °C':'немає зв\'язку'}</div><div class="mut">${d.demo?'емуляція':(d.addr||'слот порожній')}${d.offset?` · кор. ${d.offset>0?'+':''}${d.offset}`:''}</div></div>`);
    $('monDs').innerHTML=h;$('demoBar').classList.toggle('hide',!S.demo);
    const a=S.aht20,b=S.bmp280;
    $('ahtT').textContent=a.ok?f1(a.temp)+' °C':'немає даних';$('ahtH').textContent=a.ok?f1(a.humidity)+' %':'—';
    $('bmpP').textContent=b.ok?f1(b.pressure_mmhg)+' мм':'немає даних';$('bmpHpa').textContent=b.ok?f1(b.pressure_hpa)+' гПа':'—';$('bmpT').textContent=b.ok?f1(b.temp)+' °C':'—';
    $('st').textContent=(S.wifi.connected?`${S.wifi.ip} · ${S.wifi.rssi} dBm`:`точка доступу ${S.wifi.ip}`)+` · ${Math.round(S.uptime_s/60)} хв`;
    updDsLive();
  }catch(e){$('st').textContent='немає зв\'язку з пристроєм'}}

$('demo').onchange=e=>save({dsDemo:e.target.checked},e.target.checked?'Демо-режим увімкнено':'Демо-режим вимкнено');
$('owScan').onclick=async()=>{try{owFound=(await api('/api/scan/onewire')).found;
  $('owRes').textContent=owFound.length?'Знайдено: '+owFound.map(x=>x.addr+(x.slot?` (T${x.slot})`:' (не призначено)')).join(', '):'Датчики не знайдено';renderDs()}catch(e){toast(e.message)}};
$('dsSave').onclick=()=>{
  const ds=C.ds.map((_,i)=>({name:$('dn'+i).value,addr:$('da'+i).value,offset:parseFloat($('do'+i).value)||0}));
  const used=ds.map(d=>d.addr).filter(Boolean);if(new Set(used).size!=used.length)return toast('Один датчик призначено у два слоти');
  save({ds})};
$('i2cScan').onclick=async()=>{try{const f=(await api('/api/scan/i2c')).found;
  const n={0x27:'LCD',0x3f:'LCD',0x38:'AHT20',0x76:'BMP280',0x77:'BMP280'};
  $('i2cRes').textContent=f.length?'Знайдено: '+f.map(a=>'0x'+a.toString(16)+(n[a]?` (${n[a]})`:'')).join(', '):'Нічого не знайдено'}catch(e){toast(e.message)}};
$('pinsSave').onclick=()=>{
  const pins={};['sda','scl','onewire','encA','encB','encBtn'].forEach(k=>pins[k]=parseInt($('p_'+k).value));
  const [cols,rows]=$('lcdSize').value.split('x').map(Number);const addr=parseInt($('lcdAddr').value||'0',16);
  if(isNaN(addr))return toast('Невірна адреса LCD');
  save({pins,lcd:{addr,cols,rows,charset:+$('lcdCharset').value},encoder:{stepsPerDetent:+$('encSteps').value,reverse:$('encRev').checked}})};
function bars(r){return r>=-55?'▂▄▆█':r>=-67?'▂▄▆_':r>=-75?'▂▄__':'▂___'}
$('wScan').onclick=async()=>{const b=$('wScan');b.disabled=true;b.textContent='Пошук…';$('nets').innerHTML='';
  try{const n=(await api('/api/scan/wifi')).networks;
    if(!n.length)$('nets').innerHTML='<div class="mut">Мережі не знайдено</div>';
    n.forEach(x=>{const e=document.createElement('button');e.type='button';
      e.innerHTML=`<span></span><span class="mut">${bars(x.rssi)} ${x.rssi} dBm · кан. ${x.channel}${x.open?' · відкрита':' · 🔒'}</span>`;
      e.firstChild.textContent=x.ssid;
      e.onclick=()=>{$('ssid').value=x.ssid;$('nets').innerHTML='';$('wpass').focus()};$('nets').appendChild(e)});
  }catch(e){toast('Сканування: '+e.message,5000)}b.disabled=false;b.textContent='Знайти мережі'};
$('wSave').onclick=()=>{const w={ssid:$('ssid').value,hostname:$('hostname').value};
  if(dirty.has('wpass'))w.pass=$('wpass').value;if(dirty.has('appass'))w.apPass=$('appass').value;
  if(!w.hostname)return toast('Вкажіть ім\'я хоста');save({wifi:w})};
function blUi(){$('blMin').disabled=$('blMode').value!='1'}
$('blMode').onchange=blUi;
$('sysSave').onclick=()=>{const m=Math.min(60,Math.max(1,parseInt($('blMin').value)||5));
  save({pollMs:+$('poll').value,pressUnit:+$('punit').value,lcd:{blMode:+$('blMode').value,blTimeout:m*60}})};
$('reboot').onclick=async()=>{if(!confirm('Перезавантажити?'))return;await api('/api/reboot',{});toast('Перезавантаження…',8000);setTimeout(()=>location.reload(),8000)};
$('reset').onclick=async()=>{if(!confirm('Скинути ВСІ налаштування, включно з Wi‑Fi?'))return;await api('/api/factory-reset',{});toast('Скинуто. Підключіться до точки доступу пристрою.',10000)};
$('fwBtn').onclick=()=>{const f=$('fw').files[0];if(!f)return;const fd=new FormData();fd.append('fw',f);
  const x=new XMLHttpRequest();x.open('POST','/update');x.upload.onprogress=e=>$('fwSt').textContent=Math.round(e.loaded/e.total*100)+'%';
  x.onload=()=>{if(x.status==200){$('fwSt').textContent='Готово, перезавантаження…';setTimeout(()=>location.reload(),10000)}else $('fwSt').textContent='Помилка: '+x.responseText};x.send(fd)};

const FONT='000000000000005F00000007000700147F147F14242A7F2A12231308646236495522500005030000001C2241000041221C0014083E081408083E080800503000000808080808006060000020100804023E5149453E00427F400042615149462141454B311814127F1027454545393C4A49493001710905033649494936064949291E003636000000563600000814224100141414141400412214080201510906324979413E7E1111117E7F494949363E414141227F4141221C7F494949417F090901013E414151327F0808087F00417F41002040413F017F081422417F404040407F0204027F7F0408107F3E4141413E7F090909063E4151215E7F09192946464949493101017F01013F4040403F1F2040201F7F2018207F631408146303047804036151494543007F41410002040810200041417F0004020102044040404040000102040020545454787F484444383844444420384444487F3854545418087E0901020C5252523E7F0804047800447D40002040443D007F1028440000417F40007C041804787C0804047838444444387C14141408081414187C7C080404084854545420043F4440203C4040207C1C2040201C3C4030403C44281028440C5050503C4464544C44000836410000007F000000413608000804081008',DEGG='0705070000';
const CYR={'Б':'7F49494931','Г':'7F01010101','Ё':'7C55545544','Ж':'77087F0877','З':'2241494936','И':'7F1008047F','Й':'7C2111097C','Л':'403E01017F','П':'7F0101017F','У':'274848483F','Ф':'1C227F221C','Ч':'070808087F','Ш':'7F407F407F','Ъ':'017F484830','Ы':'7F4830007F','Э':'224149493E','Ю':'7F083E413E','Я':'462919097F','Д':'603E213F60','Ц':'3F20203F60','Щ':'3F203F207F','б':'3C4A4A4931','в':'7C54545428','г':'7C04040404','ё':'3855545518','ж':'6C107C106C','з':'4444545428','и':'7C2010087C','й':'7C2112097C','к':'7C10284400','л':'403804047C','м':'7C0810087C','н':'7C1010107C','п':'7C0404047C','т':'04047C0404','ч':'0C1010107C','ш':'7C407C407C','ъ':'047C505020','ы':'7C5050207C','ь':'7C50505020','э':'2844545438','ю':'7C10384438','я':'483414147C','д':'6038243C60','ф':'18247E2418','ц':'3C20203C60','щ':'3C203C207C','Є':'3E49494922','є':'3854544400','Ї':'00457C4500','ї':'00457C4100','Ґ':'7E02020203','ґ':'7C04040406'};
function glyph(ch){const c=ch.charCodeAt(0);if(ch=='°')return DEGG;if(CYR[ch])return CYR[ch];if(c<32||c>126)return FONT.substr(('?'.charCodeAt(0)-32)*10,10);return FONT.substr((c-32)*10,10)}
// HD44780-like dot matrix: 5x8 dots per cell, 1-dot gaps, unlit dots faintly visible
function drawLcd(cv,lines,cols,rows,on){
  const box=cv.closest('.card').clientWidth-60, pitch=Math.max(2,Math.min(6,Math.floor(box/(cols*6+1))));
  const W=(cols*6+1)*pitch, H=(rows*9+1)*pitch, dpr=window.devicePixelRatio||1;
  cv.width=W*dpr;cv.height=H*dpr;cv.style.width=W+'px';cv.style.height=H+'px';
  const g=cv.getContext('2d');g.scale(dpr,dpr);g.fillStyle=on?'#1a44c8':'#0d1d55';g.fillRect(0,0,W,H);
  const dot=pitch*0.86, lit=on?'#e9f0ff':'#3d5296', off=on?'rgba(255,255,255,0.07)':'rgba(255,255,255,0.03)';
  if(on){g.shadowColor='rgba(170,200,255,0.7)';}
  for(let r=0;r<rows;r++){const t=(lines[r]||'').padEnd(cols,' ');
    for(let c=0;c<cols;c++){const gl=glyph(t[c]);
      for(let x=0;x<5;x++){const col=parseInt(gl.substr(x*2,2),16);
        for(let y=0;y<8;y++){const px=(c*6+1+x)*pitch, py=(r*9+1+y)*pitch, l=y<7&&(col>>y)&1;
          g.shadowBlur=l&&on?pitch:0;g.fillStyle=l?lit:off;g.fillRect(px,py,dot,dot)}}}}
  cv.setAttribute('aria-label',lines.join('\n'))}
let lastLcd=null;
function renderLcd(j){lastLcd=j;const cv=$('lcd');cv.parentElement.style.background=j.present?'#1a44c8':'#2b3550';
  const bl=j.backlight!==false;cv.parentElement.style.background=bl?'#1a44c8':'#0d1d55';drawLcd(cv,j.lines,j.cols,j.rows,bl);
  $('lcdInfo').textContent=`${j.cols}×${j.rows}`+(j.present?'':' · фізичний дисплей не знайдено, показано емулятор')+(bl?'':' · підсвітка вимкнена')}
window.addEventListener('resize',()=>lastLcd&&renderLcd(lastLcd));
let lcdBusy=false;
async function pollLcd(){if(lcdBusy||$('t-mon').classList.contains('hide'))return;lcdBusy=true;
  try{renderLcd(await api('/api/display'))}catch(e){}lcdBusy=false}
document.querySelectorAll('.enc button').forEach(b=>b.onclick=async()=>{
  try{lcdBusy=true;renderLcd(await api('/api/display/input',JSON.parse(b.dataset.in)))}catch(e){toast(e.message)}lcdBusy=false});
pollLcd();setInterval(pollLcd,400);
fillPins();loadCfg().catch(e=>toast(e.message));poll();setInterval(poll,2000);
</script></body></html>)HTML";

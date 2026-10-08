// Auto-generated web page.
#pragma once
static const char INDEX_HTML[] = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SENTRY &middot; Environmental Node</title>
<style>
  :root{
    --base:#0A0A0B; --panel:#121316; --gold:#C8A24B; --gold-hi:#E6C478;
    --label:#8A8F98; --text:#E8E9EC; --line:#24262B;
    --ok:#4FB477; --alert:#E0563B;
    --mono:"SFMono-Regular",Consolas,"Liberation Mono",Menlo,monospace;
    --sans:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
    --serif:"Cormorant Garamond",Georgia,"Times New Roman",serif;
  }
  *{box-sizing:border-box;margin:0;padding:0}
  html,body{background:var(--base);color:var(--text);font-family:var(--sans);
    -webkit-font-smoothing:antialiased;min-height:100vh}
  body{padding:0 0 48px}
  .topbar{display:flex;align-items:center;justify-content:space-between;
    padding:18px 28px;border-bottom:1px solid var(--line);
    background:linear-gradient(180deg,#0E0E10,#0A0A0B);position:sticky;top:0;z-index:10}
  .brand{display:flex;align-items:center;gap:14px}
  .emblem{width:46px;height:46px;border-radius:50%;position:relative;flex:0 0 auto;
    border:1.5px solid #7A6433;display:flex;align-items:center;justify-content:center;
    background:radial-gradient(circle at 50% 35%,#1c1a14,#0a0a0b)}
  .emblem::before{content:"";position:absolute;inset:4px;border-radius:50%;border:1px solid rgba(200,162,75,.25)}
  .emblem span{font-family:var(--serif);font-weight:600;font-size:20px;letter-spacing:.5px;color:var(--gold-hi)}
  .wordmark{display:flex;flex-direction:column;line-height:1.05}
  .wordmark .w1{font-family:var(--serif);font-size:22px;font-weight:600;letter-spacing:3px;color:var(--gold-hi)}
  .wordmark .w2{font-size:10px;letter-spacing:5px;color:var(--label);text-transform:uppercase}
  .clockbox{text-align:right}
  .clockbox .time{font-family:var(--mono);font-size:22px;color:var(--text);letter-spacing:1px}
  .clockbox .zone{font-size:10px;letter-spacing:2px;color:var(--label);text-transform:uppercase;
    display:flex;align-items:center;gap:6px;justify-content:flex-end;margin-top:2px}
  .live{display:inline-flex;align-items:center;gap:5px}
  .dot{width:7px;height:7px;border-radius:50%;background:var(--ok);animation:pulse 2s infinite}
  .dot.off{background:var(--alert);animation:none}
  @keyframes pulse{0%{box-shadow:0 0 0 0 rgba(79,180,119,.5)}70%{box-shadow:0 0 0 7px rgba(79,180,119,0)}100%{box-shadow:0 0 0 0 rgba(79,180,119,0)}}
  .wrap{max-width:1120px;margin:0 auto;padding:0 24px}
  .eyebrow{display:flex;align-items:center;gap:14px;margin:38px 0 18px}
  .eyebrow h2{font-family:var(--serif);font-size:24px;font-weight:600;color:var(--gold-hi);letter-spacing:1px;white-space:nowrap}
  .eyebrow .rule{height:1px;background:linear-gradient(90deg,#7A6433,transparent);flex:1}
  .gauges{display:grid;grid-template-columns:repeat(4,1fr);gap:18px}
  @media(max-width:880px){.gauges{grid-template-columns:repeat(2,1fr)}}
  @media(max-width:480px){.gauges{grid-template-columns:1fr}}
  .gauge{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:22px 18px 18px}
  .gauge .glabel{font-size:11px;letter-spacing:2px;text-transform:uppercase;color:var(--label)}
  .ring{display:block;margin:8px auto 4px;width:150px;height:150px}
  .ring .val{font-family:var(--mono);font-size:30px;fill:var(--text)}
  .ring .unit{font-size:11px;fill:var(--label);letter-spacing:1px}
  .gstate{text-align:center;font-size:11px;letter-spacing:1px;height:14px;text-transform:uppercase;color:var(--ok)}
  .gstate.warn{color:var(--alert)}
  .panels{display:grid;grid-template-columns:1fr 1fr;gap:18px}
  @media(max-width:760px){.panels{grid-template-columns:1fr}}
  .panel{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:22px 24px}
  .panel h3{font-size:12px;letter-spacing:2px;text-transform:uppercase;color:var(--gold);margin-bottom:16px;display:flex;align-items:center;gap:10px}
  .panel h3 .pdot{width:6px;height:6px;border-radius:50%;background:var(--gold)}
  .row{display:flex;justify-content:space-between;align-items:baseline;padding:9px 0;border-bottom:1px solid rgba(255,255,255,.04)}
  .row:last-child{border-bottom:none}
  .row .k{font-size:13px;color:var(--label)}
  .row .v{font-family:var(--mono);font-size:13px;color:var(--text);text-align:right;word-break:break-all;max-width:60%}
  .v.badge{color:var(--gold-hi)} .v.up{color:var(--ok)} .v.down{color:var(--alert)}
  .listening{color:var(--label);font-style:italic}
  .chartcard{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:22px 24px}
  .chartcard h3{font-size:12px;letter-spacing:2px;text-transform:uppercase;color:var(--gold);margin-bottom:6px}
  .legend{display:flex;gap:18px;flex-wrap:wrap;margin:6px 0 14px;font-size:11px;color:var(--label)}
  .legend span{display:inline-flex;align-items:center;gap:6px}
  .legend i{width:10px;height:3px;border-radius:2px;display:inline-block}
  #chart{width:100%;height:240px;display:block}
  footer{text-align:center;color:var(--label);font-size:11px;letter-spacing:1px;margin-top:42px}
</style>
</head>
<body>
  <div class="topbar">
    <div class="brand">
      <div class="emblem"><span>S</span></div>
      <div class="wordmark"><div class="w1">SENTRY</div><div class="w2">Sensor Node</div></div>
    </div>
    <div class="clockbox">
      <div class="time" id="clock">--:--:--</div>
      <div class="zone"><span class="live"><span class="dot" id="livedot"></span>LIVE</span> &middot; Orlando, FL</div>
    </div>
  </div>

  <div class="wrap">
    <div id="predict" style="display:none;margin:22px 0 0;padding:12px 18px;border-radius:12px;
      background:rgba(224,169,59,.12);border:1px solid rgba(224,169,59,.4);color:#E6C478;
      font-size:14px;letter-spacing:.5px"></div>
    <div class="eyebrow"><h2>Environment</h2><div class="rule"></div></div>
    <div class="gauges" id="gauges"></div>

    <div class="eyebrow"><h2>History</h2><div class="rule"></div></div>
    <div class="chartcard">
      <h3>Sensors Over Time</h3>
      <div class="legend">
        <span><i style="background:#FF8C1A"></i>Temp &deg;F</span>
        <span><i style="background:#33C7FF"></i>Humidity %</span>
        <span><i style="background:#4FB477"></i>Pressure inHg</span>
        <span><i style="background:#E6C478"></i>Air AQI</span>
        <span style="margin-left:auto"><button id="rangeBtn" style="background:#1a1c20;color:#C8A24B;border:1px solid #2a2d33;border-radius:8px;padding:4px 12px;cursor:pointer;font-size:11px">Live</button></span>
      </div>
      <canvas id="chart" width="1040" height="240"></canvas>
    </div>

    <div class="eyebrow"><h2>Connectivity</h2><div class="rule"></div></div>
    <div class="panels">
      <div class="panel">
        <h3><span class="pdot"></span>Device Statistics</h3>
        <div class="row"><span class="k">Link</span><span class="v up" id="d_link">--</span></div>
        <div class="row"><span class="k">IP Address</span><span class="v" id="d_ip">--</span></div>
        <div class="row"><span class="k">Subnet Mask</span><span class="v" id="d_mask">--</span></div>
        <div class="row"><span class="k">Gateway</span><span class="v" id="d_gw">--</span></div>
        <div class="row"><span class="k">DNS</span><span class="v" id="d_dns">--</span></div>
        <div class="row"><span class="k">MAC</span><span class="v" id="d_mac">--</span></div>
      </div>
      <div class="panel">
        <h3><span class="pdot"></span>Switch Statistics</h3>
        <div id="sw_known" style="display:none">
          <div class="row"><span class="k">Protocol</span><span class="v badge" id="s_proto">--</span></div>
          <div class="row"><span class="k">Switch</span><span class="v" id="s_name">--</span></div>
          <div class="row"><span class="k">Port</span><span class="v" id="s_port">--</span></div>
          <div class="row"><span class="k">VLAN</span><span class="v badge" id="s_vlan">--</span></div>
          <div class="row"><span class="k">Mode</span><span class="v" id="s_mode">--</span></div>
          <div class="row"><span class="k">Mgmt IP</span><span class="v" id="s_mgmt">--</span></div>
        </div>
        <div id="sw_wait" class="listening" style="padding:9px 0">
          Listening for LLDP / CDP / FDP&hellip; up to 60s on a managed switch.
        </div>
      </div>
    </div>

    <div class="panels" style="margin-top:18px">
      <div class="panel" id="wifi_panel">
        <h3><span class="pdot"></span>WiFi / Access Point</h3>
        <div id="wifi_known" style="display:none">
          <div class="row"><span class="k">SSID</span><span class="v badge" id="w_ssid">--</span></div>
          <div class="row"><span class="k">Signal</span><span class="v" id="w_rssi">--</span></div>
          <div class="row"><span class="k">Quality</span><span class="v" id="w_qual">--</span></div>
          <div class="row"><span class="k">Channel</span><span class="v" id="w_chan">--</span></div>
          <div class="row"><span class="k">Security</span><span class="v" id="w_auth">--</span></div>
          <div class="row"><span class="k">WiFi IP</span><span class="v" id="w_ip">--</span></div>
          <div class="row"><span class="k">BSSID (AP MAC)</span><span class="v" id="w_bssid">--</span></div>
        </div>
        <div id="wifi_wait" class="listening" style="padding:9px 0">Connecting to WiFi&hellip;</div>
      </div>
      <div class="panel">
        <h3><span class="pdot"></span>System</h3>
        <div class="row"><span class="k">Uptime</span><span class="v" id="sys_uptime">--</span></div>
        <div class="row"><span class="k">Local Time</span><span class="v" id="sys_time">--</span></div>
        <div class="row"><span class="k">Weather</span><span class="v" id="sys_wx">--</span></div>
      </div>
    </div>

    <footer>SENTRY &middot; Environmental Monitoring Node</footer>
  </div>

<script>
const GA=[
  {k:"temp",label:"Temperature",unit:"\u00B0F",min:32,max:122,dec:1,color:"#FF8C1A"},
  {k:"hum", label:"Humidity",   unit:"%",       min:0,max:100,dec:0,color:"#33C7FF"},
  {k:"pres",label:"Pressure",   unit:"inHg",    min:28,max:31,dec:2,color:"#4FB477"},
  {k:"gas", label:"Air Quality",unit:"AQI",     min:0,max:100,dec:0,color:"#E6C478"},
];
const R=64,C=2*Math.PI*R,SWEEP=0.75;
function buildGauges(){
  const host=document.getElementById('gauges');
  GA.forEach(g=>{
    const el=document.createElement('div');el.className='gauge';
    el.innerHTML=`<div class="glabel">${g.label}</div>
      <svg class="ring" viewBox="0 0 160 160">
      <circle cx="80" cy="80" r="${R}" fill="none" stroke="#222A33" stroke-width="11" stroke-linecap="round" stroke-dasharray="${C*SWEEP} ${C}" transform="rotate(135 80 80)"/>
      <circle id="arc_${g.k}" cx="80" cy="80" r="${R}" fill="none" stroke="${g.color}" stroke-width="11" stroke-linecap="round" stroke-dasharray="0 ${C}" transform="rotate(135 80 80)" style="transition:stroke-dasharray .5s ease-out,stroke .3s"/>
      <text class="val" id="val_${g.k}" x="80" y="82" text-anchor="middle">--</text>
      <text class="unit" x="80" y="100" text-anchor="middle">${g.unit}</text></svg>
      <div class="gstate" id="state_${g.k}"></div>`;
    host.appendChild(el);
  });
}
function setGauge(g,value,alert,msg){
  const frac=Math.max(0,Math.min(1,(value-g.min)/(g.max-g.min)));
  const arc=document.getElementById('arc_'+g.k);
  arc.setAttribute('stroke-dasharray',`${C*SWEEP*frac} ${C}`);
  arc.setAttribute('stroke',alert?'#E0563B':g.color);
  document.getElementById('val_'+g.k).textContent=value.toFixed(g.dec);
  const st=document.getElementById('state_'+g.k);
  st.textContent=alert?msg:''; st.className='gstate'+(alert?' warn':'');
}
function evalAlert(k,v){
  if(k==='temp'){if(v>86)return'High Temp';if(v<41)return'Low Temp';}
  if(k==='hum'){if(v>70)return'High Humidity';if(v<20)return'Low Humidity';}
  if(k==='gas'){if(v<30)return'Poor Air';}
  return null;
}
buildGauges();

// ---- history chart ----
const HIST={temp:[],hum:[],pres:[],gas:[]};   // live rolling
let STORED={temp:[],hum:[],pres:[],gas:[]};   // 30-day from device
let SRC=HIST;                                  // which one the chart draws
let MAXPTS=120;
function pushHist(d){
  if(d.temp!==undefined)HIST.temp.push(d.temp);
  if(d.hum!==undefined)HIST.hum.push(d.hum);
  if(d.pres!==undefined)HIST.pres.push(d.pres);
  if(d.gas!==undefined)HIST.gas.push(d.gas);
  for(const k in HIST){ if(HIST[k].length>MAXPTS)HIST[k].shift(); }
}
async function loadStored(){
  try{
    const r=await fetch('/history'); const j=await r.json();
    STORED={temp:j.t||[],hum:j.h||[],pres:j.p||[],gas:j.a||[]};
  }catch(e){}
}
function setupRange(){
  const btn=document.getElementById('rangeBtn');
  btn.onclick=async()=>{
    if(SRC===HIST){ await loadStored(); SRC=STORED; MAXPTS=Math.max(STORED.temp.length,10); btn.textContent='30-Day'; }
    else { SRC=HIST; MAXPTS=120; btn.textContent='Live'; }
    drawChart();
  };
}
function normSeries(arr,min,max){ return arr.map(v=>(v-min)/(max-min)); }
function drawChart(){
  const cv=document.getElementById('chart');const ctx=cv.getContext('2d');
  const W=cv.width,H=cv.height,pad=8;
  ctx.clearRect(0,0,W,H);
  // grid
  ctx.strokeStyle='rgba(255,255,255,.05)';ctx.lineWidth=1;
  for(let i=0;i<=4;i++){const y=pad+(H-2*pad)*i/4;ctx.beginPath();ctx.moveTo(pad,y);ctx.lineTo(W-pad,y);ctx.stroke();}
  const series=[
    {data:normSeries(SRC.temp,32,122),color:'#FF8C1A'},
    {data:normSeries(SRC.hum,0,100),color:'#33C7FF'},
    {data:normSeries(SRC.pres,28,31),color:'#4FB477'},
    {data:normSeries(SRC.gas,0,100),color:'#E6C478'},
  ];
  const denom=Math.max((SRC.temp.length||MAXPTS)-1,1);
  series.forEach(s=>{
    if(s.data.length<2)return;
    ctx.strokeStyle=s.color;ctx.lineWidth=2;ctx.beginPath();
    s.data.forEach((v,i)=>{
      const x=pad+(W-2*pad)*i/denom;
      const y=pad+(H-2*pad)*(1-Math.max(0,Math.min(1,v)));
      i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
    });
    ctx.stroke();
  });
}

let serverEpoch=null,epochAt=0;
function tickClock(){
  let now=serverEpoch!==null?new Date(serverEpoch*1000+(Date.now()-epochAt)):new Date();
  const opt={timeZone:'America/New_York',hour:'2-digit',minute:'2-digit',second:'2-digit',hour12:false};
  const s=new Intl.DateTimeFormat('en-US',opt).format(now);
  document.getElementById('clock').textContent=s;
  document.getElementById('sys_time').textContent=s;
}
setInterval(tickClock,1000);tickClock();

function fmtUptime(sec){
  const d=Math.floor(sec/86400),h=Math.floor(sec%86400/3600),m=Math.floor(sec%3600/60);
  if(d>0)return`${d}d ${h}h ${m}m`;if(h>0)return`${h}h ${m}m`;return`${m}m`;
}
function wifiQuality(r){if(r>=-55)return'Excellent';if(r>=-67)return'Good';if(r>=-75)return'Fair';if(r>=-85)return'Weak';return'Very Weak';}

function apply(d){
  if(d.temp!==undefined)setGauge(GA[0],d.temp,evalAlert('temp',d.temp)!==null,evalAlert('temp',d.temp));
  if(d.hum!==undefined) setGauge(GA[1],d.hum, evalAlert('hum',d.hum)!==null, evalAlert('hum',d.hum));
  if(d.pres!==undefined)setGauge(GA[2],d.pres,false,null);
  if(d.gas!==undefined) setGauge(GA[3],d.gas, evalAlert('gas',d.gas)!==null, evalAlert('gas',d.gas));
  pushHist(d);
  if(SRC===HIST)drawChart();
  const pb=document.getElementById('predict');
  if(d.predict && d.predict.length>0){ pb.style.display='block'; pb.textContent='\u26A0 '+d.predict; }
  else pb.style.display='none';
  const link=document.getElementById('d_link');
  link.textContent=d.link?'UP':'DOWN';link.className='v '+(d.link?'up':'down');
  document.getElementById('d_ip').textContent=d.ip||'--';
  document.getElementById('d_mask').textContent=d.mask||'--';
  document.getElementById('d_gw').textContent=d.gw||'--';
  document.getElementById('d_dns').textContent=d.dns||'--';
  document.getElementById('d_mac').textContent=d.mac||'--';
  if(d.sw_found){
    document.getElementById('sw_known').style.display='block';
    document.getElementById('sw_wait').style.display='none';
    document.getElementById('s_proto').textContent=d.sw_proto||'--';
    document.getElementById('s_name').textContent=d.sw_name||'--';
    document.getElementById('s_port').textContent=d.sw_port||'--';
    document.getElementById('s_vlan').textContent=d.sw_vlan||'--';
    document.getElementById('s_mode').textContent=d.sw_mode||'--';
    document.getElementById('s_mgmt').textContent=d.sw_mgmt||'--';
  }
  if(d.wifi){
    document.getElementById('wifi_known').style.display='block';
    document.getElementById('wifi_wait').style.display='none';
    document.getElementById('w_ssid').textContent=d.wssid||'--';
    document.getElementById('w_rssi').textContent=(d.wrssi!==undefined?d.wrssi+' dBm':'--');
    document.getElementById('w_qual').textContent=(d.wrssi!==undefined?wifiQuality(d.wrssi):'--');
    document.getElementById('w_chan').textContent=d.wchan||'--';
    document.getElementById('w_auth').textContent=d.wauth||'--';
    document.getElementById('w_ip').textContent=d.wip||'--';
    document.getElementById('w_bssid').textContent=d.wbssid||'--';
  }
  if(d.uptime!==undefined)document.getElementById('sys_uptime').textContent=fmtUptime(d.uptime);
  if(d.epoch){serverEpoch=d.epoch;epochAt=Date.now();}
}

function connect(){
  const ws=new WebSocket('ws://'+location.host+'/ws');
  const dot=document.getElementById('livedot');
  ws.onopen=()=>{dot.className='dot';};
  ws.onclose=()=>{dot.className='dot off';setTimeout(connect,2000);};
  ws.onerror=()=>{ws.close();};
  ws.onmessage=(e)=>{try{apply(JSON.parse(e.data));}catch(err){}};
}
setupRange();
connect();
</script>
</body>
</html>

)rawhtml";

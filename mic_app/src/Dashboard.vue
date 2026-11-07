<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { invoke, isTauri } from '@tauri-apps/api/core';
import { listen, type UnlistenFn } from '@tauri-apps/api/event';
import { csv, parseSession, rejection, simulated, statistics, usable, validateTelemetry, type Sample, type Telemetry, type Trial } from './model';

type Mode = 'disconnected' | 'device' | 'simulation' | 'replay';
const mode=ref<Mode>('disconnected'), port=ref(''), ports=ref<{name:string;label:string;telemetry:boolean}[]>([]);
const error=ref(''), notice=ref(''), connecting=ref(false), reconnect=ref(false);
const latest=ref<Telemetry|null>(null), received=ref(0), clock=ref(Date.now());
const history=ref<Sample[]>([]), trials=ref<Trial[]>([]), recording=ref<Trial|null>(null);
const expected=ref(45), distance=ref('30 cm'), elevation=ref('In array plane'), notes=ref('');
const selected=ref(''), replayIndex=ref(0), replayRunning=ref(false);
const fileInput=ref<HTMLInputElement|null>(null);
let simulationTimer:ReturnType<typeof setInterval>|undefined;
let watchdog:ReturnType<typeof setInterval>|undefined;
let retry:ReturnType<typeof setTimeout>|undefined;
let replayTimer:ReturnType<typeof setTimeout>|undefined;
let origin=Date.now(), simSeq=0, lastKey='', operation=0;
const unlisten:UnlistenFn[]=[];
const activeTrial=computed(()=>recording.value ?? trials.value.find(t=>t.id===selected.value) ?? null);
const summary=computed(()=>activeTrial.value ? statistics(activeTrial.value) : null);
const stale=computed(()=>!!latest.value && mode.value!=='replay' && (clock.value-received.value+latest.value.age_ms>700));
const valid=computed(()=>!!latest.value && usable(latest.value) && !stale.value && mode.value!=='disconnected');
const status=computed(()=>mode.value==='disconnected'?'Disconnected':!latest.value?'Waiting for telemetry':!latest.value.enabled?'Estimation off':!latest.value.current?'Waiting for new estimate':stale.value?'Stale':latest.value.reject_mask?'Rejected':'Provisional');
const angle=computed(()=>valid.value?latest.value!.angle_mdeg/1000:null);
const expectedSafe=computed(()=>Number.isFinite(expected.value) && expected.value>=0 && expected.value<360);
const arrow=computed(()=>angle.value===null?'':`rotate(${-angle.value} 210 210)`);
const expectedTransform=computed(()=>`rotate(${-(recording.value?.expected ?? (mode.value==='replay'?activeTrial.value?.expected:undefined) ?? expected.value)} 210 210)`);
const noiseBusy=ref(false),saveBusy=ref(false);
const noiseAvailable=computed(()=>['device','simulation'].includes(mode.value)&&latest.value?.settings_saved!==undefined);
const beamMode=ref(1), beamAzimuth=ref(0), beamElevation=ref(0), beamBusy=ref(false);
const simBeam=ref({bf_mode:1,bf_azimuth_tdeg:0,bf_elevation_tdeg:900,bf_revision:0,bf_pending:0,bf_fade_left:0,nr_mode:2,nr_learned:1,nr_learn_left:0,nr_hum:50,nr_channel:1,settings_saved:0,uac_active:0});
let learnTimer:ReturnType<typeof setTimeout>|undefined;
const beamAvailable=computed(()=>!saveBusy.value&&['device','simulation'].includes(mode.value)&&latest.value?.bf_mode!==undefined);
const beamValid=computed(()=>beamMode.value!==2||(Number.isFinite(beamAzimuth.value)&&beamAzimuth.value>=0&&beamAzimuth.value<360&&Number.isFinite(beamElevation.value)&&beamElevation.value>=0&&beamElevation.value<=90));
const beamNormal=computed(()=>latest.value?.bf_mode===1||latest.value?.bf_elevation_tdeg===900);
const beamLabel=computed(()=>latest.value?.bf_mode===undefined?'Unavailable in this firmware/session':latest.value.bf_mode===0?'Beamforming off':beamNormal.value?'Broadside · face normal':`Manual · ${(latest.value.bf_azimuth_tdeg!/10).toFixed(1)}° azimuth · ${(latest.value.bf_elevation_tdeg!/10).toFixed(1)}° elevation`);
function simulationFrame(){return {...simulated(++simSeq),...simBeam.value};}
async function applyBeam(){
  if(!beamAvailable.value||!beamValid.value)return;
  const azimuth=beamMode.value===2?Math.round(beamAzimuth.value*10)%3600:0,elevation=beamMode.value===2?Math.round(beamElevation.value*10):900;
  error.value='';beamBusy.value=true;
  try{
    if(mode.value==='simulation'){
      simBeam.value={...simBeam.value,bf_mode:beamMode.value,bf_azimuth_tdeg:azimuth,bf_elevation_tdeg:elevation,bf_revision:simBeam.value.bf_revision+1,nr_learned:0,settings_saved:0};
      accept(simulationFrame());beamBusy.value=false;notice.value='Simulated beam updated; simulation does not process audio.';
    }else await invoke('beam',{mode:beamMode.value,azimuth,elevation});
  }catch(e){beamBusy.value=false;error.value=String(e);}
}
async function toggleNoise(){
  if(!noiseAvailable.value)return;
  clearTimeout(learnTimer);noiseBusy.value=true;error.value='';
  const target=latest.value?.nr_mode===2?0:2;
  try{
    if(mode.value==='simulation'){
      simBeam.value.nr_learn_left=0;simBeam.value.nr_mode=target;simBeam.value.nr_learned=0;simBeam.value.settings_saved=0;
      accept(simulationFrame());noiseBusy.value=false;
    }else await invoke('noise',{mode:target,hum:latest.value?.nr_hum??50,channel:latest.value?.nr_channel??1});
  }catch(e){noiseBusy.value=false;error.value=String(e);}
}
async function saveSettings(){
  if(!noiseAvailable.value)return;
  saveBusy.value=true;error.value='';
  try{
    if(mode.value==='simulation'){
      simBeam.value.settings_saved=1;accept(simulationFrame());saveBusy.value=false;
      notice.value='Simulated save only; no device settings were written.';
    }else{
      reconnect.value=true;await invoke('save_settings');
      notice.value='Saving audio settings and restarting. The app will reconnect automatically.';
    }
  }catch(e){saveBusy.value=false;error.value=String(e);}
}
function useEstimate(){if(valid.value){beamMode.value=2;beamAzimuth.value=(Math.round(angle.value!*10)%3600)/10;}}
async function learnNoise(){
  try{
    if(mode.value==='simulation'){
      clearTimeout(learnTimer);simBeam.value.nr_learn_left=96000;
      learnTimer=setTimeout(()=>{simBeam.value.nr_learn_left=0;simBeam.value.nr_learned=1;},2000);
    }else await invoke('learn_noise');
    notice.value='Keep the source silent for two seconds while the background noise profile is learned.';
  }catch(e){error.value=String(e);}
}
const pairNames=['1–2','1–3','1–4','2–3','2–4','3–4'];
const chart=computed(()=>{
  const source=mode.value==='replay'?history.value:history.value.filter(s=>s.at>=(history.value.at(-1)?.at??0)-20000);
  if(!source.length)return [];
  const end=source.at(-1)!.at,start=Math.max(0,end-20000),span=20000;
  return source.map(s=>({x:46+(s.at-start)/span*706,y:125-s.data.angle_mdeg/1000/360*100,ok:usable(s.data)}));
});
function accept(data:Telemetry, at=Date.now()-origin, record=true) {
  if(!validateTelemetry(data)){error.value='Invalid telemetry packet';return;}
  if(saveBusy.value&&data.settings_error){saveBusy.value=false;error.value='Save canceled because audio recording started. Close recording and save again.';}
  latest.value=data;received.value=Date.now();clock.value=received.value;
  const key=`${data.epoch}:${data.seq}`;
  if(key===lastKey)return;
  lastKey=key;
  const sample={at,data:JSON.parse(JSON.stringify(data)) as Telemetry};
  history.value.push(sample);if(history.value.length>1500)history.value.shift();
  if(record && recording.value) {
    recording.value.samples.push({at:Date.now()-Date.parse(recording.value.started),data:JSON.parse(JSON.stringify(data)) as Telemetry});
    if(recording.value.samples.length>=10000){finishTrial();notice.value='Trial stopped at 10,000 samples.';}
  }
}
async function refreshPorts(){
  try{
    if(isTauri()){
      ports.value=await invoke('ports');
      const preferred=ports.value.find(p=>p.telemetry);
      if(preferred && mode.value==='disconnected')port.value=preferred.name;
    }
  }catch(e){error.value=String(e);}
}
function clearTimers(){clearTimeout(learnTimer);beamBusy.value=false;noiseBusy.value=false;saveBusy.value=false;clearInterval(simulationTimer);clearTimeout(retry);clearTimeout(replayTimer);replayRunning.value=false;}
function finishTrial(){if(recording.value){trials.value.push(recording.value);selected.value=recording.value.id;recording.value=null;notice.value='Trial saved in memory. Export the session to keep it after closing.';}}
async function detach(){
  operation++;finishTrial();clearTimers();mode.value='disconnected';latest.value=null;lastKey='';
  if(isTauri())await invoke('disconnect');
}
async function connectDevice(automatic=false){
  if(connecting.value)return;
  if(!isTauri()){error.value='Serial access requires the desktop app. Use Simulation in the browser.';return;}
  if(!automatic)await detach();
  const token=++operation;connecting.value=true;error.value='';
  try{
    await invoke('connect',{port:port.value});
    if(token!==operation)return;
    mode.value='device';saveBusy.value=false;noiseBusy.value=false;origin=Date.now();history.value=[];latest.value=null;lastKey='';
    notice.value='Subscribed to live telemetry. Enable estimation if the board is in its boot-default state.';
  }catch(e){if(token!==operation)return;error.value=String(e);mode.value='disconnected';if(reconnect.value)retry=setTimeout(()=>connectDevice(true),2000);}
  finally{connecting.value=false;}
}
async function startSimulation(){await detach();mode.value='simulation';origin=Date.now();history.value=[];simSeq=0;accept(simulationFrame());simulationTimer=setInterval(()=>accept(simulationFrame()),171);}
async function toggleEstimation(){
  try{await invoke('estimation',{enabled:latest.value?.enabled!==1});}catch(e){error.value=String(e);}
}
function startTrial(){
  if(!expectedSafe.value||!['device','simulation'].includes(mode.value)||!latest.value?.current||latest.value.enabled!==1)return;
  recording.value={id:crypto.randomUUID(),started:new Date().toISOString(),mode:mode.value,expected:expected.value,distance:distance.value,elevation:elevation.value,notes:notes.value,samples:[]};
  notice.value='Recording unique estimates. Keep the source and board fixed.';
}
async function save(format:'json'|'csv'){
  const source=recording.value?[...trials.value,recording.value]:trials.value;
  if(!source.length)return;
  const content=format==='json'?JSON.stringify({schema:'mic_app.session',version:1,trials:source},null,2):csv(source);
  const name=`mic_app-${new Date().toISOString().slice(0,10)}.${format}`;
  try{
    if(isTauri()){if(await invoke('export_report',{name,content}))notice.value=`Saved ${format.toUpperCase()}.`;}
    else {const url=URL.createObjectURL(new Blob([content],{type:format==='json'?'application/json':'text/csv'}));const a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
  }catch(e){error.value=String(e);}
}
async function importSession(event:Event){
  const input=event.target as HTMLInputElement,file=input.files?.[0];if(!file)return;
  try{
    if(file.size>50*1024*1024)throw new Error('Session exceeds 50 MB');
    const session=parseSession(await file.text());
    await detach();trials.value.push(...session.trials.map(t=>({...t,id:crypto.randomUUID()})));
    selected.value=trials.value.at(-1)?.id??'';mode.value='replay';showFrame(0);notice.value='Imported session. Replay retains its original device/simulation labels.';
  }catch(e){error.value=String(e);}finally{input.value='';}
}
function showFrame(index:number){
  const t=activeTrial.value;if(!t)return;
  replayIndex.value=Math.max(0,Math.min(index,t.samples.length-1));lastKey='';
  history.value=t.samples.slice(Math.max(0,replayIndex.value-1500),replayIndex.value);
  const s=t.samples[replayIndex.value];if(s)accept(s.data,s.at,false);else latest.value=null;
}
function replayStep(){
  const samples=activeTrial.value?.samples;if(!samples?.length)return;
  showFrame(replayIndex.value);
  if(replayIndex.value+1>=samples.length){replayRunning.value=false;return;}
  const delay=Math.min(2000,Math.max(20,samples[replayIndex.value+1]!.at-samples[replayIndex.value]!.at));
  replayIndex.value++;replayTimer=setTimeout(replayStep,delay);
}
async function playTrial(){
  const id=selected.value;await detach();selected.value=id;mode.value='replay';history.value=[];showFrame(0);replayRunning.value=true;replayStep();
}
function pauseReplay(){clearTimeout(replayTimer);replayRunning.value=false;}
watch(selected,()=>{if(mode.value==='replay'){pauseReplay();showFrame(0);}});
function scrub(event:Event){pauseReplay();showFrame(Number((event.target as HTMLInputElement).value));}
function fmt(v:number|null|undefined,suffix='°'){return v===null||v===undefined?'—':v.toFixed(1)+suffix;}
function dbfs(rms:number){return rms>0?20*Math.log10(rms/1000/32768):-96;}
onMounted(async()=>{
  if(isTauri()){
    unlisten.push(await listen<{control?:string}>('control-ack',e=>{if(e.payload.control==='nr_config'){noiseBusy.value=false;notice.value='Noise settings applied. Use Save & restart to retain them after power cycling.';}if(e.payload.control==='beam'){beamBusy.value=false;notice.value='Beam command accepted; applied settings are shown below.';}}));
    unlisten.push(await listen<string>('control-error',e=>{beamBusy.value=false;noiseBusy.value=false;saveBusy.value=false;error.value=e.payload;}));
    unlisten.push(await listen<Telemetry>('telemetry',e=>{if(mode.value==='device')accept(e.payload);}));
    unlisten.push(await listen<string>('connection-error',e=>{
      if(mode.value!=='device')return;
      finishTrial();error.value=e.payload;mode.value='disconnected';latest.value=null;
      if(reconnect.value)retry=setTimeout(()=>connectDevice(true),2000);
    }));
  }
  await refreshPorts();watchdog=setInterval(()=>clock.value=Date.now(),100);
});
onUnmounted(()=>{clearTimers();clearInterval(watchdog);unlisten.forEach(f=>f());if(isTauri())void invoke('disconnect');});
</script>

<template>
  <main>
    <header class="topbar">
      <div class="brand"><span class="brand-icon">◉</span><div><h1>mic_app</h1><p>Direction lab <span>/ RP2354A · 4 microphones</span></p></div></div>
      <div class="connection"><select v-model="port" aria-label="Serial port" :disabled="mode==='device'||connecting"><option v-if="!port" value="" disabled>Select telemetry port</option><option v-else-if="!ports.some(p=>p.name===port)" :value="port">{{ port }}</option><option v-for="p in ports" :key="p.name" :value="p.name">{{ p.label }}</option></select><button @click="refreshPorts" title="Refresh serial ports">↻</button><button class="primary" :disabled="connecting||(!port&&mode!=='device')" @click="mode==='device'?detach():connectDevice()">{{ connecting?'Connecting…':mode==='device'?'Disconnect':'Connect board' }}</button><button :class="{selected:mode==='simulation'}" @click="startSimulation">Simulation</button></div>
    </header>
    <div class="subbar"><span class="mode-pill" :class="mode">{{ mode.toUpperCase() }}</span><span>48 kHz · GCC-PHAT · azimuth only</span><label><input type="checkbox" v-model="reconnect"> Auto reconnect</label><button v-if="mode!=='disconnected'&&mode!=='device'" @click="detach">Stop {{ mode }}</button><button v-if="mode==='device'" @click="toggleEstimation">{{ latest?.enabled===1?'Disable':'Enable' }} estimation</button></div>
    <div v-if="error" class="banner error" role="alert">{{ error }} <button @click="error=''">Dismiss</button></div>
    <div v-if="notice" class="banner notice" role="status">{{ notice }}<button @click="notice=''">×</button></div>
    <div class="workspace">
      <section class="panel direction">
        <div class="panel-heading"><div><span class="eyebrow">LIVE DIRECTION</span><h2>Exposed-port view</h2></div><span class="status" :class="{good:valid,bad:status==='Rejected'||stale}"><i></i>{{ status }}</span></div>
        <svg class="compass" viewBox="0 0 420 420" role="img" aria-label="Azimuth compass, zero right and ninety top">
          <defs><radialGradient id="field"><stop offset="0" stop-color="#163b3d"/><stop offset="1" stop-color="#101b24"/></radialGradient></defs>
          <circle cx="210" cy="210" r="158" fill="url(#field)" stroke="#314452"/>
          <circle v-for="r in [53,105]" :key="r" cx="210" cy="210" :r="r" fill="none" stroke="#263b47" stroke-dasharray="3 5"/>
          <path d="M52 210H368M210 52V368" stroke="#314452"/>
          <g v-for="n in 24" :key="n" :transform="`rotate(${n*15} 210 210)`"><path d="M359 210h9" stroke="#6b8290"/></g>
          <g v-if="expectedSafe" :transform="expectedTransform"><path d="M210 210H366" stroke="#e8bb73" stroke-width="2" stroke-dasharray="5 6"/><circle cx="366" cy="210" r="5" fill="#e8bb73"/></g>
          <g v-if="valid" :transform="arrow"><path d="M210 210L351 210" stroke="#66e1c3" stroke-width="4"/><path d="M353 210L337 201V219Z" fill="#66e1c3"/></g>
          <g v-if="latest?.bf_mode && !beamNormal" :transform="`rotate(${-(latest.bf_azimuth_tdeg??0)/10} 210 210)`" data-testid="beam-marker"><path d="M280 210H351" stroke="#bb9aff" stroke-width="3"/><path d="M351 202V218" stroke="#bb9aff" stroke-width="3"/></g>
          <rect x="155" y="155" width="110" height="110" rx="18" fill="#162630" stroke="#48616b"/>
          <rect x="256" y="160" width="14" height="25" rx="3" fill="#95a8af"/><text x="282" y="162" class="svg-small">USB-C</text>
          <g v-for="m in [{x:210,y:166,c:1},{x:166,y:210,c:2},{x:210,y:254,c:3},{x:254,y:210,c:4}]" :key="m.c"><circle :cx="m.x" :cy="m.y" r="7" fill="#71dfc4"/><text :x="m.x" :y="m.y+22" text-anchor="middle" class="svg-small">{{ m.c }}</text></g>
          <circle v-if="latest?.bf_mode && beamNormal" cx="210" cy="210" r="14" fill="none" stroke="#bb9aff" stroke-width="3" data-testid="beam-marker"/>
          <circle cx="210" cy="210" r="4" fill="#eaf4f3"/>
          <text x="210" y="29" text-anchor="middle" class="svg-label">90° · TOP</text><text x="210" y="404" text-anchor="middle" class="svg-label">270° · BOTTOM</text><text x="9" y="190" class="svg-label">180°</text><text x="377" y="190" class="svg-label">0°</text>
        </svg>
        <div class="angle-readout">{{ angle===null?'—':angle.toFixed(1)+'°' }}<span>{{ valid?'Provisional azimuth':'No usable direction' }}</span></div>
        <p class="reason">{{ latest?.reject_mask?rejection(latest.reject_mask).join(' · '):'Accepted estimates are provisional, not calibrated accuracy.' }}</p>
        <div class="legend"><span><i class="mint"></i>Estimate</span><span><i class="gold"></i>Expected source</span><span><i style="background:#bb9aff"></i>Applied beam</span></div>
        <p class="footnote">Sound ports face you; USB-C upper right. Front/back is ambiguous. Elevation and distance are not estimated.</p>
      </section>
      <div class="right-column">
        <section class="metrics"><div class="metric"><span>UPDATE RATE</span><strong>{{ latest?.interval_ms?(1000/latest.interval_ms).toFixed(1):'—' }} <small>Hz</small></strong></div><div class="metric"><span>NEWEST-SAMPLE AGE</span><strong>{{ latest?((latest.end_age_us+(mode==='replay'?0:Math.max(0,clock-received)*1000))/1000).toFixed(0):'—' }} <small>ms</small></strong></div><div class="metric"><span>FIT RESIDUAL</span><strong>{{ latest?(latest.fit_milli/1000).toFixed(3):'—' }} <small>samples</small></strong></div></section>
        <section class="panel beam-controls">
          <div class="panel-heading"><div><span class="eyebrow">ONBOARD AUDIO</span><h2>Manual beamforming</h2></div><span class="muted">{{ mode==='simulation'?'Simulated controls':'UAC output' }}</span></div>
          <div class="beam-form">
            <label>Beam mode<select v-model.number="beamMode" aria-label="Beam mode" :disabled="!beamAvailable"><option :value="0">Off</option><option :value="1">Broadside</option><option :value="2">Manual</option></select></label>
            <label>Azimuth (°)<input v-model.number="beamAzimuth" aria-label="Beam azimuth" type="number" min="0" max="359.9" step="0.1" :disabled="!beamAvailable||beamMode!==2"></label>
            <label>Assumed elevation (°)<input v-model.number="beamElevation" aria-label="Beam elevation" type="number" min="0" max="90" step="0.1" :disabled="!beamAvailable||beamMode!==2"></label>
            <button class="primary" @click="applyBeam" :disabled="!beamAvailable||!beamValid||beamBusy">{{ beamBusy?'Applying…':'Apply beam' }}</button>
            <button @click="useEstimate" :disabled="!beamAvailable||!valid">Use current estimate</button>
          </div>
          <p class="beam-applied" data-testid="beam-applied">{{ beamLabel }}<span v-if="latest?.bf_pending||latest?.bf_fade_left"> · transitioning</span></p>
          <p class="footnote">0° azimuth is right; 90° is top. Elevation 0° lies in the board plane; 90° points out of its face. Elevation is assumed, not estimated. Apply sends a fixed target; it does not track the estimate.</p>
          <div class="health noise-settings">
            <span>Noise suppression <b>{{ latest?.nr_mode===2?'Enabled':'Off' }}</b></span>
            <button @click="toggleNoise" :disabled="!noiseAvailable||noiseBusy||saveBusy||!!latest?.uac_active">{{ noiseBusy?'Applying...':latest?.nr_mode===2?'Disable suppression':'Enable suppression' }}</button>
          </div>
          <div class="health"><span>Noise profile <b>{{ latest?.nr_mode!==2?'Suppression off':latest?.nr_learn_left?'Learning…':latest?.nr_learned?'Learned':'Relearn after steering' }}</b></span><button @click="learnNoise" :disabled="!beamAvailable||latest?.nr_mode!==2||!!latest?.nr_learn_left||!!latest?.bf_pending||!!latest?.bf_fade_left">Relearn noise</button></div>
          <div class="health persistence"><span>{{ latest?.settings_saved===1?'Audio settings saved on board':'Unsaved audio settings' }}</span><button @click="saveSettings" :disabled="!noiseAvailable||saveBusy||noiseBusy||beamBusy||!!latest?.nr_learn_left||!!latest?.uac_active||!!latest?.bf_pending||!!latest?.bf_fade_left">{{ saveBusy?'Saving...':'Save & restart' }}</button></div>
          <p class="footnote">Save &amp; restart retains suppression, hum filter, input channel and beam target after power cycling. Close audio recording before switching suppression or saving. A learned noise profile is saved too; relearn it when the room or beam target changes.</p>
          <p class="footnote">The beam is mono, copied to all four USB channels. Raw microphones still feed direction estimation. Steering clears the learned noise profile.</p>
        </section>
        <section class="panel history"><div class="panel-heading"><div><span class="eyebrow">RECENT ESTIMATES</span><h2>Azimuth history</h2></div><span class="muted">20-second window</span></div><svg viewBox="0 0 780 150" role="img" aria-label="Direction history; rejected estimates appear below the chart"><g v-for="a in [0,90,180,270,360]" :key="a"><path :d="`M46 ${125-a/360*100}H752`" stroke="#283946"/><text x="4" :y="129-a/360*100" class="svg-small">{{ a }}°</text></g><circle v-for="(p,i) in chart" :key="i" :cx="p.x" :cy="p.ok?p.y:139" :r="p.ok?2.4:1.8" :fill="p.ok?'#66e1c3':'#d89576'"/></svg><p class="footnote">Rejected, disabled and old results leave gaps; orange marks indicate unusable estimates.</p></section>
        <section class="panel signals"><div class="panel-heading"><div><span class="eyebrow">RAW MICROPHONES</span><h2>Levels & pair delays</h2></div><span class="muted">Pre-DSP</span></div><div class="signal-layout"><div class="levels"><div v-for="c in 4" :key="c" class="level-row"><span>CH{{ c }}</span><div class="meter"><i :style="{width:Math.max(0,Math.min(100,(dbfs(latest?.rms_milli[c-1]??0)+96)/96*100))+'%'}"></i></div><b>{{ latest?dbfs(latest.rms_milli[c-1]!).toFixed(1):'—' }} <small>dBFS</small></b></div><p class="footnote">Window RMS · digital level, not SPL</p></div><table><thead><tr><th>Pair</th><th>Delay / samples</th><th>Coherence</th></tr></thead><tbody><tr v-for="(pair,i) in pairNames" :key="pair"><td>{{ pair }}</td><td>{{ latest?(latest.lag_milli[i]!/1000).toFixed(3):'—' }}</td><td>{{ latest?(latest.coherence_milli[i]!/1000).toFixed(2):'—' }}</td></tr></tbody></table></div><div class="health"><span>Capture drops <b>{{ latest?.dropped??'—' }}</b></span><span>Faults <b>{{ latest?.faults??'—' }}</b></span><span>Overwritten windows <b>{{ latest?.overwritten??'—' }}</b></span><span>Resets <b>{{ latest?.resets??'—' }}</b></span></div></section>
      </div>
    </div>
    <section class="panel validation"><div class="panel-heading"><div><span class="eyebrow">CONTROLLED-SOURCE VALIDATION</span><h2>Measure, compare, repeat</h2></div><div class="actions"><button @click="fileInput?.click()">Import session</button><button :disabled="!trials.length&&!recording" @click="save('json')">Export JSON</button><button :disabled="!trials.length&&!recording" @click="save('csv')">Export CSV</button><input ref="fileInput" type="file" accept=".json" hidden @change="importSession"></div></div>
      <div class="trial-form"><label>Expected azimuth <div class="unit-input"><input v-model.number="expected" type="number" min="0" max="359.9" step="0.1" :disabled="!!recording"><span>°</span></div></label><label>Source distance <input v-model="distance" :disabled="!!recording" placeholder="e.g. 40 cm"></label><label>Elevation / placement <input v-model="elevation" :disabled="!!recording"></label><label class="notes">Notes <input v-model="notes" :disabled="!!recording" placeholder="Source, volume, room…"></label><button v-if="recording" class="record stop" @click="finishTrial">■ Finish trial · {{ recording.samples.length }}</button><button v-else class="record" :disabled="!expectedSafe||!latest?.current||latest.enabled!==1||!['device','simulation'].includes(mode)" @click="startTrial">● Start trial</button></div>
      <div v-if="trials.length" class="session-controls"><select v-model="selected" :disabled="!!recording" aria-label="Saved trial"><option v-for="t in trials" :key="t.id" :value="t.id">{{ t.mode.toUpperCase() }} · {{ t.expected }}° · {{ t.samples.length }} estimates · {{ new Date(t.started).toLocaleTimeString() }}</option></select><button :disabled="!!recording||!activeTrial?.samples.length" @click="playTrial">Replay trial</button><button v-if="mode==='replay'" @click="replayRunning?pauseReplay():(replayRunning=true,replayStep())">{{ replayRunning?'Pause':'Play' }}</button><input v-if="mode==='replay'" aria-label="Replay position" type="range" min="0" :max="Math.max(0,(activeTrial?.samples.length??1)-1)" :value="replayIndex" @input="scrub"></div>
      <div v-if="summary" class="trial-stats"><div><span>Acceptance</span><b>{{ (summary.acceptance*100).toFixed(1) }}% <small>({{ summary.accepted }}/{{ summary.total }})</small></b></div><div><span>Mean direction</span><b>{{ fmt(summary.mean) }}</b></div><div><span>Signed bias</span><b>{{ fmt(summary.bias) }}</b></div><div><span>Mean absolute error</span><b>{{ fmt(summary.mae) }}</b></div><div><span>95th percentile error</span><b>{{ fmt(summary.p95) }}</b></div><div><span>Circular deviation</span><b>{{ fmt(summary.deviation) }}</b></div></div>
      <p class="footnote">Use a fixed broadband source at a known position. Statistics use unique, current accepted results. Simulation trials are labeled separately. Export sessions before closing; they are held in memory.</p>
    </section>
    <footer>mic_app <span>Prototype direction estimates · nominal 50 mm opposite-microphone spacing · no calibrated angular resolution</span><span v-if="latest">Result #{{ latest.seq }} / epoch {{ latest.epoch }}</span></footer>
  </main>
</template>

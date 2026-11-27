// Adapter for the preserved preview's renderer. It does not open a hardware connection.
(() => {
  const keys={up:1,down:2,menu:4,sel:8};
  CV.tabIndex=0;
  CV.setAttribute('aria-label','Simulated TFT. Arrow keys, M and Enter operate the four buttons.');
  const keyboard={ArrowUp:'up',ArrowDown:'down',m:'menu',Enter:'sel'};
  const {debounceMs,longMs,restartMs,powerMs}=SNTCH_CONFIG;
  let raw=0,stable=0,suppress=0,combo=0,latched=false,began=0,offer=0,activity=0;
  let wifi=true,link=true,action='Booting simulated panel',pending='',qrFault='',requestId=0;
  const debounce=[0,0,0,0], down=[0,0,0,0];
  const set=(k,v)=>{
    const bit=keys[k], next=v?raw|bit:raw&~bit;
    if(raw!==next) {raw=next;debounce[Math.log2(bit)]=performance.now();}
  };
  const resetKeys=()=>{raw=stable=suppress=combo=0;latched=false;};
  const row=document.createElement('div');row.className='row';
  const act=document.createElement('div');act.className='ev';act.id='simulation-action';act.setAttribute('aria-live','polite');
  function control(label,fn) {const b=document.createElement('button');b.textContent=label;b.onclick=fn;row.append(b);return b;}
  const wifiButton=control('Simulate Wi-Fi loss',()=>{wifi=!wifi;wifiButton.textContent=wifi?'Simulate Wi-Fi loss':'Restore simulated Wi-Fi'; network();});
  const linkButton=control('Simulate UART error',()=>{link=!link;linkButton.textContent=link?'Simulate UART error':'Restore simulated UART';network();});
  $('now').parentElement.append(row,act);
  document.querySelector('.note').textContent='Simulation only: no actual AP, UART, classroom data, or usable credentials. Use M + Enter for QR restart, or both arrows for standby and wake. A single pointer can use the keyboard for the other button.';
  for(const [i,lines] of [
    [0,['SIMULATED CONTROLLER','WI-FI SIMULATION','CLIENTS UNAVAILABLE','FIRMWARE UNAVAILABLE']],
    [3,['SIMULATED CLASSROOM','NO LIVE SESSION DATA']],
    [4,['HELP DATA UNAVAILABLE','NO CONTROLLER FEED']],
    [5,['LITTLEFS UNAVAILABLE','ESP32 FLASH STORAGE']],
    [6,['SIMULATED UART','FOUR BUTTONS CONFIGURED']],
    [7,['RESTART CONTROLLER?','','SEL CONFIRM','MENU CANCEL']],
    [8,['SOFTWARE STANDBY?','','SEL CONFIRM','MENU CANCEL']]
  ]) MENU[i][2]=lines;

  function boot() {
    ++requestId; stopDemo(); pending=qrFault=''; toFace(); go('boot',true);
    const request=requestId;
    setTimeout(()=>{if(request===requestId && screen==='face' && state==='boot')go(!link||!wifi?'lost':'standby');},3200);
  }
  function network() {
    ++requestId; stopDemo(); pending='';
    action=!link?'UART response timed out':!wifi?'Wi-Fi disconnected':'Connection recovering';
    if(screen==='off')return;
    if(screen==='qr') {
      if(link&&wifi) openQr(qrKind,false); else qrFault=!link?'LINK TIMEOUT':'WI-FI NOT READY';
    } else {
      toFace();go(!link||!wifi?'lost':'recovery');
      const request=requestId;
      if(link&&wifi)setTimeout(()=>{if(request===requestId&&state==='recovery')go('ready');},1150);
    }
  }
  function openQr(kind,restart) {
    stopDemo();
    action=(restart?'QR session restart requested: ':'Requested ')+kind;
    openScreen('qr',{kind},1);
    pending='WAITING FOR QR';qrFault='';
    const id=++requestId;
    setTimeout(()=>{
      if(id!==requestId||screen!=='qr'||qrKind!==kind)return;
      pending='';
      if(!link||!wifi) {
        qrFault=!link?'LINK TIMEOUT':'WI-FI NOT READY'; action=qrFault;return;
      }
      if(restart) {if(kind==='controller')++offer;else ++activity;}
      const code=kind==='controller'?offer.toString(16).toUpperCase().padStart(8,'0'):'SIM'+activity.toString(36).toUpperCase().padStart(5,'0');
      const q=QRS[kind];
      q.code=code;q.room=kind==='activity'?'SIM LIVE':'SIM READY';
      q.payload=kind==='controller'?`sntch://pair?url=http://192.0.2.1&sid=${code}`:code;
      q.ev='Browser-only sample, not a real session or network address.';
      delete QRC[kind]; qrT0=T;
      action=restart?'QR session refreshed (simulated)':'QR matrix received (simulated)';
    },link?300:1000);
  }
  function standby() {
    ++requestId; pending=qrFault=''; action='Software standby (both boards would still be powered)';
    openScreen('off',null,1);
  }
  function wake() {
    action='Wake requested'; toFace();go(link&&wifi?'recovery':'lost');
    const id=++requestId;
    if(link&&wifi)setTimeout(()=>{if(id===requestId&&state==='recovery')go('ready');},1150);
  }
  const original=press;
  press=function(k,held=false) {
    if(k==='power'){boot();return;}
    if(screen==='off')return;
    stopDemo();
    action=`${held?'Long press':'Short press'} · ${k==='sel'?'SELECT':k.toUpperCase()}`;
    if(k==='menu') {
      ++requestId; pending=qrFault='';
      if(screen==='face') {menuSel=0;openScreen('menu',null,1);}
      else if(screen==='menu') toFace();
      else openScreen('menu',null,-1);
      return;
    }
    if(k==='sel' && !held) {
      if(screen==='menu'&&(menuSel===1||menuSel===2)){openQr(menuSel===1?'controller':'activity',false);return;}
      if(screen==='detail'&&detailIdx===8){standby();return;}
      if(screen==='detail'&&detailIdx===7){boot();return;}
      if(screen==='qr'&&qrFault){openQr(qrKind,false);return;}
    }
    if(k!=='sel'||!held)original(k);
  };
  $('qrC').onclick=()=>{if(screen!=='off')openQr('controller',false);};
  $('qrA').onclick=()=>{if(screen!=='off')openQr('activity',false);};
  $('menuB').onclick=()=>{if(screen!=='off'){action='Open device menu';stopDemo();menuSel=0;openScreen('menu',null,1);}};
  document.querySelectorAll('[data-k]').forEach(b=>{
    const k=b.dataset.k; if(!(k in keys))return;
    b.onclick=e=>{if(e.detail===0 && !raw && !stable)press(k);};
    b.addEventListener('pointerdown',e=>{e.preventDefault();b.setPointerCapture(e.pointerId);set(k,true);});
    ['pointerup','pointercancel','lostpointercapture'].forEach(n=>b.addEventListener(n,()=>set(k,false)));
    b.style.touchAction='none';
  });
  window.addEventListener('keydown',e=>{
    const k=keyboard[e.key];if(!k||e.altKey||e.ctrlKey||e.metaKey||e.target.matches('input,select,[contenteditable]'))return;
    e.preventDefault();set(k,true);
  });
  window.addEventListener('keyup',e=>{if(keyboard[e.key]){e.preventDefault();set(keyboard[e.key],false);}});
  window.addEventListener('blur',resetKeys);
  renderOff=function(g) {
    g.fillStyle='#000';g.fillRect(0,0,160,128);
    const line='SOFTWARE STANDBY';drawText(line,Math.floor(80-textW(line)/2),60,UI.mute,g);
  };
  renderQR=function(g) {
    const q=QRS[qrKind];
    g.fillStyle=UI.bg;g.fillRect(0,0,160,128);hdr(g,q.title,pending||qrFault?'warn':q.tone);
    if(pending||qrFault) {
      drawText(pending||qrFault,8,36,UI.ink,g);
      if(qrFault)drawText('SELECT TO RETRY',8,51,UI.body,g);
    } else {
      const qr=QRC[qrKind]||(QRC[qrKind]=qrEncode(q.payload)),N=qr.size,Q=4,M=Math.floor(90/(N+2*Q));
      const prog=Math.max(0,Math.min(1,(T-qrT0)/.6)),rows=Math.floor(prog*N),box=(N+Q*2)*M;
      g.fillStyle='#fff';g.fillRect(8,24,box,box);g.fillStyle=UI.dark;
      for(let y=0;y<rows;y++)for(let col=0;col<N;col++)if(qr.m[y][col])g.fillRect(8+(col+Q)*M,24+(y+Q)*M,M,M);
      if(prog<1){g.fillStyle=UI.acc;g.fillRect(10,24+(rows+Q)*M,box-4,1);}
      drawText(q.who,104,26,UI.acc,g);drawText(q.a,104,38,UI.body,g);drawText(q.b,104,48,UI.body,g);
      drawText(qrKind==='activity'?'CODE':'OFFER ID',104,66,UI.mute,g);drawText(q.code,104,76,UI.ink,g);
      drawText(q.room,104,90,UI.body,g);
      drawText(prog<1?'BUILDING':'READY',104,104,prog<1?UI.warn:UI.acc,g);
    }
    foot(g,'SIM ONLY','MENU EXIT');
  };
  const baseDraw=draw;
  draw=function() {
    const now=performance.now();
    let released=0;
    for(let i=0;i<4;i++){
      const bit=1<<i;
      if(((raw^stable)&bit)&&now-debounce[i]>=debounceMs) {
        stable^=bit;if(stable&bit)down[i]=now;else released|=bit;
      }
    }
    if(stable&&(stable&(stable-1))){
      suppress|=stable;
      if((stable!==3&&stable!==12)||(combo&&combo!==stable))latched=true;
      if(!latched&&!combo){combo=stable;began=now;action=combo===3?'Power hold started':'QR restart hold started';}
    }
    if(combo&&stable!==combo){combo=0;if(!latched)action='Hold cancelled · no single button action';latched=true;}
    const p=combo?Math.min(1,(now-began)/(combo===3?powerMs:restartMs)):0;
    if(combo&&combo===raw&&!latched&&p===1){
      latched=true;
      if(combo===3){if(screen==='off')wake();else standby();}
      else if(screen!=='off')openQr(screen==='qr'?qrKind:'controller',true);
    }
    for(const [i,k] of ['up','down','menu','sel'].entries())if((released&(1<<i))&&!(suppress&(1<<i))&&!latched)press(k,now-down[i]>=longMs);
    if(!stable){latched=false;suppress=0;combo=0;}
    baseDraw();
    if(combo){
      main.fillStyle=UI.bg;main.fillRect(4,96,152,19);
      drawText(combo===3?'POWER HOLD':'QR SESSION RESTART',8,98,UI.ink,main);
      main.fillStyle=UI.hdr;main.fillRect(8,108,144,4);main.fillStyle=UI.acc;main.fillRect(8,108,p*144,4);
    }
    if(act.textContent!==action)act.textContent=action;
    CV.dataset.screen=screen;CV.dataset.expression=state;CV.dataset.qr=qrKind;
    if(screen==='off'){$('now').textContent='Software standby';$('ev').textContent='Hold UP + DOWN again to wake. Browser only.';}
    if(screen==='menu')$('ev').textContent='Use UP and DOWN to choose a menu item, SELECT to open, MENU to return.';
  };
  boot();
})();

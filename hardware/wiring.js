(() => {
  'use strict';
  const $=id=>document.getElementById(id);
  const svg=$('diagram');
  const {parts,pins,wires,colors,boards}=SNTCH_WIRING;
  const modes={full:'Full wiring diagram',tft:'Arduino ↔ TFT',buttons:'Arduino ↔ Buttons',uart:'Arduino ↔ ESP32',rf:'Antenna',
    power:'Power distribution',flow:'System communication flow',preview:'Hardware preview'};
  const steps=[
    ['Common ground','With power off, link the half-size GND rail to the mini board’s shaded ground strip. Verify continuity to the ESP-32U development board, UNO, both translators and TFT; check whether the breadboard rails are split.'],
    ['Antenna, USB and 3.3 V power','Attach a compatible 2.4 GHz antenna before enabling Wi-Fi. Power/program the ESP-32U board through its micro-USB port; its onboard regulator powers the module. Do not connect UNO 5 V to ESP32 GPIO or 3V3. Use the board 3V3 output for translator/peripheral loads only after its current headroom is verified.'],
    ['Display signal interface','Route SCK, MOSI, CS, D/C and RESET through U1 74LVC245 from A to B (DIR high, /OE low). SPI is write-only. D6 is optional for a verified backlight logic EN; if unused, tie A7 pin 8 low and leave B7 pin 12 open.'],
    ['Four buttons','UP and DOWN go on the half-size board; MENU and SELECT go on the mini. Use opposite switch contacts and check that each switch is open at rest. Connect the mini returns to the same strip as the ground link.'],
    ['Bidirectional UART','Route UNO A1/TX through U1 to ESP32 GPIO16/RX2. Route GPIO17/TX2 through one enabled gate of U2 74HCT125D (VCC=UNO 5 V, /OE low) to UNO A0/RX. UART2 is separate from USB UART0 and UNO D0/D1.'],
    ['Check unpowered wiring','Use the validator, verify the actual pinout and measure continuity, insulation between rails and the button contacts. The browser cannot check your bench.'],
    ['Upload each target','Upload uno_display over UNO USB and esp32_dual through the ESP-32U board micro-USB port. The board’s USB-UART bridge and EN/BOOT buttons are onboard. Initialize LittleFS only on confirmed blank hardware.'],
    ['Bring-up and tests','Check the regulated rails under load, then both UART directions, menu buttons, hold cancellation, both QR screens and reset/recovery. See the build notes.']
  ];
  const flowData={
    'Status update':['ESP32 checks AP, server and classroom state','UNO polls with sequence + CRC','Status response carries actual counters and storage state','UNO selects the face or refreshes a menu'],
    'Short / long press':['Button pulls UNO GPIO low','Debounce → short or long on release','UNO changes the menu locally','UNO reports the event; critical requests get separate responses'],
    'QR request / restart':['MENU + SELECT held for 2.5 s, or choose a QR item','UNO requests metadata; ESP32 validates the selected offer','ESP32 encodes and returns verified fixed-size chunks on demand','UNO verifies whole-matrix CRC; scans in the QR from top to bottom'],
    'Standby / wake':['UP + DOWN held for 3 s','ESP32 checkpoints LittleFS and acknowledges','UNO sleeps the TFT; verified optional backlight turns off','Release the pair then hold again to wake and resync'],
    'One board resets':['Timeout or new link epoch detected','UNO re-handshakes without replaying an uncertain mutation','ESP32 reports current service/session state','UNO recovers the screen, or shows a recoverable error']
  };
  let mode='full', step=0, chosen='', component='', hovered='', flow=Object.keys(flowData)[0];
  const changes={};
  const boardNodes={}, pinNodes={}, wireNodes={};
  const el=(tag,text,cls)=>{
    const n=document.createElement(tag); if(text!==undefined) n.textContent=text;if(cls)n.className=cls;return n;
  };
  function s(tag,attrs={},parent=svg,text) {
    const n=document.createElementNS('http://www.w3.org/2000/svg',tag);
    for(const [k,v] of Object.entries(attrs)) n.setAttribute(k,v);
    if(text!==undefined)n.textContent=text;parent.append(n);return n;
  }
  const box=(g,x,y,w,h,fill,rx=4,stroke='',cls='')=>s('rect',{x,y,width:w,height:h,fill,rx,stroke,class:cls},g);
  const txt=(g,x,y,v,sz=12,color='#d9f1e7',anchor='start',attrs={})=>s('text',
    {x,y,fill:color,'font-size':sz,'font-family':'ui-monospace,monospace','text-anchor':anchor,...attrs},g,v);
  const dot=(g,x,y,r,fill,stroke='none')=>s('circle',{cx:x,cy:y,r,fill,stroke},g);
  const trace=(g,d,stroke='#9ab7aa',width=1)=>s('path',{d,fill:'none',stroke,'stroke-width':width},g);
  function tag(g,x,y,label,state='owned') {
    const [bg,fg]={owned:['#163c31','#9ceac7'],extra:['#263d54','#c0dff4']}[state];
    box(g,x,y,label.length*6+12,15,bg,4);
    txt(g,x+6,y+10.7,label,9.5,fg,'start',{'font-weight':700});
  }
  const buttonize=(n,label,fn)=>{
    n.setAttribute('tabindex','0'); n.setAttribute('role','button'); n.setAttribute('aria-label',label);
    n.addEventListener('click',fn);
    n.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();fn(e);}});
  };
  function drawPart(p) {
    const {id,x,y,w,h}=p;
    const g=s('g',{class:'component','data-part':id},boardLayer);
    boardNodes[id]=g;
    buttonize(g,p.title,()=>selectComponent(id));
    if(id==='uno') {
      s('path',{d:`M${x+8},${y} H${x+w-19} l19,20 v228 l-16,23 H${x+10} q-10,0 -10,-11 v-252 q0,-8 8,-8z`,
        fill:'#14627f',stroke:'#8db4c1',class:'outline'},g);
      for(const [a,b] of [[x+12,y+18],[x+14,y+253],[x+w-18,y+40],[x+w-22,y+244]]) dot(g,a,b,6,'#14303c','#d0dbb5');
      box(g,x-16,y+79,79,62,'#a5b7bf',5,'#e2eaec');box(g,x-16,y+91,37,39,'#647583');txt(g,x+30,y+114,'USB-B',10,'#213241','middle');
      box(g,x-8,y+183,67,39,'#273235',3,'#647583');dot(g,x+5,y+203,12,'#151d21');dot(g,x+5,y+203,3,'#9aa4a7');
      box(g,x+116,y+111,155,54,'#19242b',5,'#618292');txt(g,x+193,y+143,'ATMEGA328P',12,'#d9e1e4','middle');
      for(let i=0;i<14;i++)for(const a of [y+108,y+165])box(g,x+121+i*10,a,4,5,'#b8cbd0',1);
      box(g,x+66,y+6,236,17,'#152633');box(g,x+93,y+245,224,17,'#152633');
      box(g,x+76,y+57,29,11,'#aebdc0');dot(g,x+218,y+77,4,'#9ad773');
      txt(g,x+114,y+55,'ARDUINO  UNO R3',21,'#e7faf9');txt(g,x+114,y+185,'5 V · ATmega328P · SPI MASTER',11);
      txt(g,x+71,y+219,'POWER',10);txt(g,x+230,y+219,'ANALOG IN',10);
      tag(g,x+242,y+67,'ON HAND');
    } else if(id==='esp') {
      box(g,x,y,w,h,'#174d44',7,'#8aaa9b','outline');
      box(g,x+4,y+25,14,h-48,'#122f28',3,'#59776a');
      box(g,x+w-18,y+25,14,h-48,'#122f28',3,'#59776a');
      box(g,x+24,y+16,w-48,145,'#a9b4b4',5,'#e0e9e7');
      txt(g,x+w/2,y+49,'ESP-32U',15,'#263c40','middle',{'font-weight':700});
      txt(g,x+w/2,y+69,'Wi-Fi / Bluetooth module',8,'#263c40','middle');
      box(g,x+w-53,y+30,19,18,'#d5c9a9',3,'#f1dfaf');
      dot(g,x+w-43.5,y+39,4.2,'#243238','#cfae64');
      txt(g,x+w-43,y+58,'U.FL',7,'#213a34','middle');
      txt(g,x+w/2,y+177,'38-PIN DEV BOARD',9,'#e4f4ec','middle');
      box(g,x+28,y+192,43,37,'#18282b',4,'#8eaaa0');
      txt(g,x+49.5,y+208,'USB-UART',7,'#e6efe8','middle');
      txt(g,x+49.5,y+219,'bridge',7,'#e6efe8','middle');
      box(g,x+83,y+192,37,37,'#394a42',4,'#9ab4a7');
      txt(g,x+101.5,y+207,'3V3',8,'#d8f1df','middle');
      txt(g,x+101.5,y+218,'reg.',7,'#d8f1df','middle');
      txt(g,x+w/2,y+243,'IC MARKINGS UNREADABLE',6.5,'#d3e4dc','middle');
      box(g,x+37,y+h-61,26,19,'#354f46',4,'#c8d7cf');
      box(g,x+w-63,y+h-61,26,19,'#354f46',4,'#c8d7cf');
      txt(g,x+50,y+h-48,'EN',7,'#effcf3','middle');
      txt(g,x+w-50,y+h-48,'BOOT',7,'#effcf3','middle');
      box(g,x+w/2-27,y+h-32,54,29,'#9da9ab',4,'#e3e8e8');
      box(g,x+w/2-15,y+h-26,30,18,'#26323a',2,'#657a7e');
      txt(g,x+w/2,y+h-5,'MICRO-USB',7,'#e4f4ec','middle');
      txt(g,x+w/2,y+h+12,'5 V USB IN · PROGRAMMING',7.5,'#a7bfb9','middle');
      tag(g,x+25,y+120,'PHOTO MATCH');
    } else if(id==='antenna') {
      box(g,x+4,y+26,52,51,'#243d36',7,'#a5c5b2');
      s('path',{d:`M${x+14},${y+30} v-12 q0,-8 16,-8 q16,0 16,8 v12 M${x+22},${y+30} v-12 q0,-4 8,-4 q8,0 8,4 v12`,
        fill:'none',stroke:'#c9d4c8','stroke-width':3},g);
      box(g,x+56,y+42,35,17,'#d3bd79',4,'#f4e2a0');
      txt(g,x+49,y+94,'2.4 GHz',9,'#f4f1dc','middle');
      txt(g,x+49,y+105,'U.FL / IPEX',7,'#d5e5dc','middle');
    } else if(id==='usbhost') {
      box(g,x,y,w,h,'#263d36',8,'#9cbaaa');
      box(g,x+13,y+19,38,37,'#415a50',5,'#bfcec4');
      txt(g,x+32,y+42,'USB',9,'#f5fff8','middle');
      txt(g,x+63,y+29,'5 V POWER',9,'#e8f6ed');
      txt(g,x+63,y+46,'DATA / FLASH',8,'#c4ddd0');
      txt(g,x+63,y+61,'NO UART ADAPTER',7,'#c4ddd0');
    } else if(id==='tft') {
      box(g,x,y,w,h,'#2d745b',9,'#87a898','outline');
      for(const [a,b] of [[x+10,y+12],[x+w-12,y+13],[x+w-12,y+h-13]])dot(g,a,b,5,'#10261d','#9bbcaf');
      box(g,x+57,y+19,142,151,'#060e17',6,'#6f8d85');
      box(g,x+65,y+29,126,111,'#adb4bd',2);
      s('image',{href:'mascot.svg',x:x+78,y:y+37,width:102,height:94},g);
      box(g,x+78,y+145,100,7,'#29553e',2);
      txt(g,x+106,y+190,'1.8 TFT SPI 128x160 v1.1',11,'#e8f7e9','middle');
      txt(g,x+106,y+209,'160 × 128 LANDSCAPE · VERIFY TAB',9,'#cee3d8','middle');
      tag(g,x+143,y+159,'ON HAND');
    } else if(id==='bread'||id==='mini') {
      SNTCH_BREADBOARDS.draw(g,id==='bread'?boards.half:boards.mini);
    } else if(['up','down','menu','select'].includes(id)) {
      box(g,x,y,w,h,'#263b34',5,'#526b5d','outline');
      for(const a of [x+8,x+w-8]) for(const b of [y+8,y+h-8])dot(g,a,b,4,'#749182');
      trace(g,`M${pins[id+'.IN'].x},${y-5} v9 M${pins[id+'.RET'].x},${y+h-4} v13`,'#a4b4b0',4);
      dot(g,x+w/2,y+31,19,'#507c67','#aed4bc');
      dot(g,x+w/2,y+30,14,id==='select'?'#3fa77c':'#172a23');
      txt(g,x+w/2,y+59,p.title,9,'#f2fee9','middle');
    } else {
      const fill={lvc:'#2a574b',upshift:'#274657',support:'#36554a',program:'#344353',reg:'#4c4736'}[id];
      box(g,x,y,w,h,fill,8,'#739589','outline');
      txt(g,x+10,y+22,p.title,id==='lvc'?13:12);
      if(id==='lvc') {
        box(g,x+60,y+39,67,118,'#18282b',6,'#637d77');
        txt(g,x+93,y+88,'LVC245',11,'#d6e0dc','middle');
        txt(g,x+93,y+106,'DIR: A → B',8,'#b9ded0','middle');
        txt(g,x+93,y+122,'5 → 3.3 V',8,'#b9ded0','middle');
        for(let i=0;i<8;i++)trace(g,`M${x+51},${y+44+i*15} h9 m67,0 h9`,'#bfcab7',3);
        tag(g,x+60,y+165,'ADDITIONAL','extra');
      } else if(id==='upshift') {
        box(g,x+82,y+37,43,34,'#18282b',4,'#748b88');
        txt(g,x+104,y+58,'HCT125D',7,'#d9e7e2','middle');txt(g,x+105,y+86,'3.3 → 5 V',10,'#c7ebec','middle');
        tag(g,x+74,y+26,'ADDITIONAL','extra');
      } else if(id==='support') {
        tag(g,x+171,y+8,'ADDITIONAL','extra');
        const resistor=(a,b)=>trace(g,`M${a},${b} h12 l3,-4 l6,8 l6,-8 l6,8 l6,-8 l3,4 h18`,'#f1c980',2);
        resistor(x+64,y+36);resistor(x+64,y+100);
        trace(g,`M${x+126},${y+35} v17 h-8 m-2,-6 v12 m-4,-12 v12 m0,-6 h-14 v14`,'#c9e4df',2);
        txt(g,x+141,y+52,'10k / 1µF',10);txt(g,x+141,y+105,'10k',10);
        txt(g,x+20,y+122,'RESET + BOOT TO GND',9);
      } else if(id==='reg') {
        box(g,x+53,y+35,66,34,'#1f302e',4,'#879486');txt(g,x+85,y+57,'3.30 V',14,'#a9e4ad','middle');
        txt(g,x+83,y+94,'≥ 500 mA + LOADS',9,'#edebca','middle');
        tag(g,x+91,y+72,'ADDITIONAL','extra');
      } else {
        box(g,x+145,y+38,42,38,'#8f9c9d');box(g,x+100,y+43,32,24,'#11212c');
        txt(g,x+67,y+72,'UART0 ONLY',9,'#d6e6eb','middle');
        tag(g,x+106,y+83,'ADDITIONAL','extra');
      }
    }
  }
  const defs=s('defs');
  const pattern=s('pattern',{id:'grid',width:24,height:24,patternUnits:'userSpaceOnUse'},defs);
  trace(pattern,'M24 0H0V24','#243946',.8);
  box(svg,0,0,1390,846,'url(#grid)',0);
  txt(svg,27,26,'LOGICAL SIGNAL MAP / NOT TO SCALE / ESP32 HEADER ORDER UNVERIFIED',12,'#8db5a4');
  const boardLayer=s('g'), wireLayer=s('g'), pinLayer=s('g');
  Object.values(parts).forEach(drawPart);
  for (const p of Object.values(pins)) {
    const g=s('g',{class:'pin','data-pin':p.key},pinLayer);
    pinNodes[p.key]=g;
    const onBreadboard=['bread','mini'].includes(p.c);
    const r=p.c==='esp'||onBreadboard?3:4.9;
    dot(g,p.x,p.y,r,p.kind==='ground'?'#233740':'#111c24',p.c==='esp'?'#dbbc65':'#bfd4cd');
    s('title',{},g,`${parts[p.c].title} · ${p.label}`);
    if(!onBreadboard) {
      if(p.align.startsWith('vertical')) {
        const b=p.align==='vertical-bottom';
        txt(g,p.x+3,p.y+(b?-8:10),p.label,p.c==='esp'?6.4:9,'#e1f4ef',b?'start':'end',
          {transform:`rotate(-90 ${p.x+3} ${p.y+(b?-8:10)})`});
      } else {
        const x=p.x+(p.align==='start'?8:p.align==='end'?-8:0);
        txt(g,x,p.y+p.dy,p.label,p.c==='esp'?8:10,'#e5f4ef',p.align);
      }
    }
    g.addEventListener('click',()=>{
      const list=visible().filter(w=>w.from===p.key||w.to===p.key);
      if(list.length===1) selectWire(list[0].id); else selectComponent(p.c);
    });
  }
  function actual(w) { return {...w,...changes[w.id]}; }
  function visible() {
    return wires.map(actual).filter(w=>!w.disabled && (step?w.step===step:mode==='full'||w.groups.includes(mode)));
  }
  function wirePath(w) {
    const a=pins[w.from],b=pins[w.to];
    const via=changes[w.id]?.to && wires.find(a=>a.id===w.id).to!==w.to?[]:w.via;
    const points=[[a.x,a.y],...via,[b.x,b.y]];
    let d=`M${points[0].join(',')}`;
    for(let i=1;i<points.length-1;++i){
      const [x,y]=points[i], [l,t]=points[i-1],[r,u]=points[i+1];
      const m=Math.min(9,Math.hypot(l-x,t-y)/2,Math.hypot(r-x,u-y)/2);
      const h1=Math.hypot(l-x,t-y)||1,h2=Math.hypot(r-x,u-y)||1;
      d+=` L${x+(l-x)/h1*m},${y+(t-y)/h1*m} Q${x},${y} ${x+(r-x)/h2*m},${y+(u-y)/h2*m}`;
    }
    return d+` L${b.x},${b.y}`;
  }
  const endpoint=k=>`${parts[pins[k].c].title} / ${pins[k].label}`;
  function mkWires() {
    wireLayer.replaceChildren();
    for(const w of wires.map(actual)) {
      const g=s('g',{class:'wire','data-wire':w.id},wireLayer);
      wireNodes[w.id]=g;
      const d=wirePath(w);
      s('path',{d,class:'lead',stroke:colors[w.color][0],'stroke-dasharray':w.local||w.option?'6 5':'none'},g);
      s('path',{d,class:'hit'},g);
        s('title',{},g,`${w.signal} · ${endpoint(w.from)} → ${endpoint(w.to)} · ${w.color==='rf'?'RF coax only':`${w.volts} V`}`);
      buttonize(g,w.signal,()=>selectWire(w.id));
      g.addEventListener('mouseenter',()=>{hovered=w.id;sync();inspect();});
      g.addEventListener('mouseleave',()=>{hovered='';sync();inspect();});
    }
  }

  function validate() {
    const issues=[];
    const issue=(state,message,id)=>issues.push({state,message,id});
    const driver=$('driver').value, power=$('tft-power').value;
    for(const [value,same,matched,missing,wrong] of [
      [driver,SNTCH_CONFIG.display.driver,'TFT driver matches the selected firmware profile.','Confirm TFT controller, SPI mode and init tab.','Display driver / init geometry differs from firmware.'],
      [power,'3.3','TFT 3.3 V supply marked verified.','TFT VCC and backlight requirements are unknown.','Proposed 3.3 V feed is incompatible with the selected TFT supply.']]) {
      issue(!value?'unverified':value===same?'confirmed':'invalid',!value?missing:value===same?matched:wrong);
    }
    issue('unverified','Photos show an onboard 3V3 regulator and USB-UART bridge, but their IC markings/current rating and board flash size are unreadable. Verify these before final wiring or upload.');
    issue($('regulator').checked?'confirmed':'unverified',$('regulator').checked?'3V3 output and available current measured with the intended peripheral load.':'Measure the board 3V3 output under load before powering translators or the TFT from it.');
    if($('peripheral').checked) issue('invalid','Existing peripheral conflicts with assigned SPI / UART pins; review CS, signal direction and bus sharing.');
    const occupied=new Map();
    const edited=Object.keys(changes).length;
    for (const original of wires) {
      const w=actual(original);
      if(w.disabled) {
        if(!w.option) issue('invalid',`Missing ${w.color==='ground'?'ground: ':''}${w.signal}.`,w.id);
        continue;
      }
      const a=pins[w.from], b=pins[w.to];
      for(const p of [a,b]) {
        if(!p.allowed) issue('invalid',`${endpoint(p.key)} is not supported by this firmware mapping.`,w.id);
        if(p.kind!=='ground'&&p.c!=='bread') {
          const another=occupied.get(p.key);
          if(another && another!==w.id) {
            issue('invalid',`Duplicate pin / GPIO conflict at ${endpoint(p.key)}.`,w.id);
            issue('invalid',`Duplicate pin / GPIO conflict at ${endpoint(p.key)}.`,another);
          }
          occupied.set(p.key,w.id);
        }
      }
      if(w.color==='rf') {
        if(w.to!==original.to) issue('invalid',`${w.signal} no longer matches the configured antenna endpoint.`,w.id);
        continue;
      }
      if(w.volts>b.v && b.v===3.3) issue('invalid',`Unsafe ${w.volts} V to 3.3 V: ${w.signal}.`,w.id);
      if(w.volts && b.kind==='ground') issue('invalid',`Driven ${w.signal} shorted to ground.`,w.id);
      if(!w.volts && b.kind!=='ground') issue('invalid',`Incorrect ground reference: ${w.signal}.`,w.id);
      if(w.groups.includes('uart') && ['uartb','uartrxb','uartrxa','uarta'].includes(w.id) &&
        (!['out','tx'].includes(a.kind)||!['rx','in'].includes(b.kind)||w.to!==original.to))
        issue('invalid',`Incorrect TX/RX direction or UART endpoint: ${w.signal}.`,w.id);
      if(w.to!==original.to) issue('invalid',`${w.signal} no longer matches the configured endpoint.`,w.id);
    }
    if(!edited) issue('proposed','Jumpers match the reference netlist. This does not confirm physical wiring.');
    if(!$('bl-confirmed').checked) issue('unverified','D6 backlight path is optional and off in the default firmware.');
    const invalid=new Set(issues.filter(i=>i.state==='invalid').map(i=>i.id));
    const status=w=>{
      if(invalid.has(w.id) || (w.option==='tft'&&power==='5') ||
        (w.groups.includes('tft')&&driver==='other')) return 'invalid';
      if((w.option==='bl'&&!$('bl-confirmed').checked) ||
        (w.option==='tft'&&!power) || (w.id==='p3dev'&&!$('regulator').checked)) return 'unverified';
      if((w.option==='tft'&&power==='3.3') || (w.id==='p3dev'&&$('regulator').checked)) return 'confirmed';
      return 'proposed';
    };
    return {issues,status};
  }
  const badge=(v)=>el('span',v,`badge ${v}`);
  function refreshValidation() {
    const {issues,status}=validate();
    $('validation').replaceChildren(...issues.map(({state,message})=>{
      const li=el('li');li.append(badge(state),document.createTextNode(` ${message}`));return li;
    }));
    const relevant=visible();
    $('wire-count').textContent=`· ${relevant.length} segments`;
    $('connections').replaceChildren(...relevant.map(w=>{
      const row=el('tr');row.dataset.wire=w.id;
      const col=(child)=>{const t=el('td');t.append(child);row.append(t);};
      const b=el('button',endpoint(w.from));b.onclick=()=>selectWire(w.id);col(b);
      col(endpoint(w.to));col(w.signal);col(w.color==='rf'?'RF coax':w.volts?`${w.volts} V`:'0 V');
      const swatch=el('span',colors[w.color][1]),chip=el('i',undefined,'chip');
      chip.style.background=colors[w.color][0];swatch.prepend(chip);col(swatch);
      col(badge(status(w)));col(w.notes);
      row.addEventListener('click',e=>{if(e.target!==b)selectWire(w.id);});
      return row;
    }));
    sync();inspect();
  }
  function inspect() {
    const w=wires.find(w=>w.id===(hovered||chosen)), p=parts[component], info=$('inspect-info');
    if(w) {
      const a=actual(w);$('inspect-title').textContent=a.signal;
      const d=el('dl');
      for(const [k,v] of [['From',endpoint(a.from)],['To',endpoint(a.to)],['Level',a.color==='rf'?'RF coax only':`${a.volts} V · ${colors[a.color][1]}`],['Status',validate().status(a)]])d.append(el('dt',k),el('dd',v));
      info.replaceChildren(d,el('p',a.notes));
    } else if(p) {
      $('inspect-title').textContent=p.title;
      const endpoints=Object.values(pins).filter(q=>q.c===p.id && visible().some(w=>w.from===q.key||w.to===q.key));
      info.replaceChildren(el('p',p.role),el('p',p.power));
      if(endpoints.length) info.append(el('p',endpoints.map(q=>q.label).join(' · ')));
    } else {
      $('inspect-title').textContent='Explore the assembly';
      info.textContent='Pick a board, pin, cable, or table row to show its endpoint details.';
    }
  }
  function sync() {
    const list=visible(), ids=new Set(list.map(w=>w.id)), available=new Set(list.flatMap(w=>[pins[w.from].c,pins[w.to].c]));
    if(mode==='full' && !step)Object.keys(parts).forEach(k=>available.add(k));
    const focus=hovered||chosen, focused=list.filter(w=>focus?w.id===focus:component&&(pins[w.from].c===component||pins[w.to].c===component));
    const active=new Set(focused.flatMap(w=>[w.from,w.to]));
    for(const w of list) if(!focus&&component) for(const key of [w.from,w.to]) if(pins[key].c===component) active.add(key);
    const highlight=!!(focus||component);
    for(const w of wires) {
      const n=wireNodes[w.id],yes=focused.some(a=>a.id===w.id);
      n.classList.toggle('hidden',!ids.has(w.id));n.classList.toggle('active',yes);
      n.classList.toggle('quiet',highlight&&!yes); n.setAttribute('aria-pressed',yes);
    }
    for(const [k,n] of Object.entries(boardNodes)) {
      const hit=(k===component)||[...active].some(p=>pins[p].c===k);
      n.classList.toggle('hidden',!available.has(k)); n.classList.toggle('active',hit);
      n.classList.toggle('quiet',highlight&&!hit);
    }
    for(const [k,n] of Object.entries(pinNodes)) {
      n.style.display=available.has(pins[k].c)?'':'none';
      n.classList.toggle('active',active.has(k));
    }
    document.querySelectorAll('tr[data-wire]').forEach(t=>t.classList.toggle('active',t.dataset.wire===focus));
  }
  function selectWire(id) {chosen=chosen===id?'':id;component='';sync();inspect();}
  function selectComponent(id) {component=component===id?'':id;chosen='';sync();inspect();}
  function reflectView() {
    const diagram=mode!=='flow'&&mode!=='preview';
    $('diagram-view').hidden=$('diagram-details').hidden=!diagram;
    $('preview-view').hidden=mode!=='preview'; $('flow-view').hidden=mode!=='flow';
    $('view-name').textContent=step?`Step ${step} · ${steps[step-1][0]}`:modes[mode];
    for(const b of $('modes').children)b.setAttribute('aria-pressed',b.dataset.mode===mode);
    const explanation=steps[step-1]?.[1]||'Choose a step to isolate its connections. Steps 6 and 8 are inspection and test only.';
    $('step-info').textContent=explanation;
    $('steps').querySelectorAll('button').forEach((b,i)=>b.setAttribute('aria-pressed',i+1===step));
    refreshValidation();
  }
  function chooseView(v) { mode=v;step=0;component=chosen=hovered='';reflectView(); }
  for(const [k,n] of Object.entries(modes)) {
    const b=el('button',n);b.dataset.mode=k;b.onclick=()=>chooseView(k);$('modes').append(b);
  }
  steps.forEach(([name],i)=>{
    const li=el('li'),b=el('button',name);li.append(b);$('steps').append(li);
    b.onclick=()=>{step=step===i+1?0:i+1;mode='full';component=chosen=hovered='';reflectView();};
  });
  function showFlow() {
    $('flow-lanes').replaceChildren();
    const lane=el('div',undefined,'flow');
    flowData[flow].forEach((msg,i)=>{const c=el('div');c.append(el('b',`0${i+1}  ${i===3?'■':'→'}`),el('span',msg));lane.append(c);});
    $('flow-lanes').append(lane);
    for(const b of $('flows').children)b.setAttribute('aria-pressed',b.textContent===flow);
  }
  for(const name of Object.keys(flowData)) {
    const b=el('button',name);b.onclick=()=>{flow=name;showFlow();};$('flows').append(b);
  }
  $('reset-view').onclick=()=>chooseView('full');
  $('baud').textContent=`UART2 · ${SNTCH_CONFIG.baud} 8N1`;
  for(const k of ['driver','tft-power','regulator','peripheral','bl-confirmed'])$(k).addEventListener('change',refreshValidation);
  const op=(name,v)=>{const n=el('option',name);n.value=v;return n;};
  for(const w of wires)$('edit-wire').append(op(w.signal,w.id));
  for(const p of Object.values(pins))$('edit-pin').append(op(endpoint(p.key),p.key));
  function syncEdit() {
    const w=actual(wires.find(w=>w.id===$('edit-wire').value));
    $('edit-pin').value=w.to; $('edit-disable').checked=!!w.disabled;
  }
  $('edit-wire').onchange=syncEdit;
  $('edit-apply').onclick=()=>{
    const id=$('edit-wire').value, to=$('edit-pin').value, disabled=$('edit-disable').checked;
    if(to===wires.find(w=>w.id===id).to&&!disabled) delete changes[id]; else changes[id]={to,disabled};
    chosen=disabled?'':id; component=hovered='';
    mkWires();refreshValidation();
  };
  $('edit-reset').onclick=()=>{Object.keys(changes).forEach(k=>delete changes[k]);chosen=hovered='';syncEdit();mkWires();refreshValidation();};
  syncEdit();mkWires();showFlow();reflectView();
})();

// Alternate ESP32-only architecture: GPIO labels follow the photo-identified dev board,
// but this is not a physical header-layout template.
(() => {
  'use strict';
  const $=id=>document.getElementById(id), svg=$('solo-diagram'), cfg=SNTCH_CONFIG.solo;
  const {half}=SNTCH_BREADBOARDS.solo;
  const palette={spi:'#36cf9c',data:'#e8ce75',cs:'#d394f0',reset:'#f994bc',button:'#a9dc72',power:'#fbab59',ground:'#c5cdd3'};
  const modes={all:'Full wiring',power:'Power & ground',tft:'TFT',buttons:'Four buttons',sd:'microSD'};
  const steps=[
    ['power','Verify board and power','Power the ESP-32U development board through micro-USB. Its onboard regulator powers the ESP32; verify 3V3 output current before using that header pin for the TFT. Bridge a split breadboard rail only if measured. Never connect a second supply to the board 3V3 pin.'],
    ['tft','Connect TFT first','Use 3.3 V SPI only after checking VCC, signals and the backlight requirements. Do not connect a pin marked LED to a GPIO. The TFT uses write-only SPI and its own CS.'],
    ['buttons','Connect four buttons','Put all four on the one half-size board. Select the layout that matches your switch pin spacing in Exact breadboard holes. On the same side of the gap, signal and return need different numbered columns; test that they only join when pressed.'],
    ['tft','Upload and test esp32_solo','Use the integrated esp32_solo target. Connect to its Wi-Fi with your phone, open Controller QR, then start a classroom session in the app.']
  ];
  const phoneSteps=[
    ['power','Verify power and ground','Keep USB unplugged while wiring. The development board boots when USB power is applied. Attach a compatible external antenna before Wi-Fi use, and verify regulator capacity for the display and SD module.'],
    ['tft','Connect the TFT display','Use the documented ESP32 SPI pins and a verified 3.3 V-compatible display. Check the exact TFT voltage and backlight requirements.'],
    ['sd','Connect the microSD SPI module','GPIO18 and GPIO23 are shared with the TFT; GPIO19 is MISO and GPIO27 is the card-select. Verify module logic, pin labels, and board pin availability.'],
    ['all','Prepare the card and upload firmware','Follow the Option C setup guide to provision a FAT32 card, then upload esp32_phone_admin. After power-up, connect the phone to the controller Wi-Fi and open the SNTCH app.']
  ];
  const parts={
    source:{title:'USB 5 V + data source',x:48,y:52,w:275,h:82,fill:'#393b30'},
    esp:{title:'ESP-32U development board · 38-pin',x:68,y:163,w:260,h:305,fill:'#174d44',
       info:'Photo shows two 19-pin headers, micro-USB, EN/BOOT buttons and an IPEX/U.FL-style antenna socket. Verify the GPIO labels on your board before wiring. USB-UART/regulator IC markings and current rating are unreadable.'},
    tft:{title:'1.8 TFT SPI 128x160 v1.1',x:704,y:126,w:250,h:165,fill:'#2a624d',
      info:'Verify actual 3.3 V VCC and 3.3 V SPI, header labels and LED requirements. SCL/SDA may mean SPI SCK/MOSI on this breakout. LED is not a GPIO signal. Don’t assume its pin order.'},
    sd:{title:'microSD SPI module',x:704,y:314,w:250,h:140,fill:'#384f58',
       info:'Option C uses a prepared FAT32 card. Verify module voltage, 3.3 V logic, current draw, and actual pin labels. The module must release MISO when its chip-select is high.'},
    rail:{title:'Half-size breadboard',...half,
      info:'Optional half-size breadboard for organizing shared 3.3 V and GND connections. The no-button option has no switch wiring. Use the + rail only for verified peripherals and measure continuity first because some boards have split rails.'}
  };
  function element(tag,text) {const n=document.createElement(tag);if(text!==undefined)n.textContent=text;return n;}
  function make(tag,attrs,parent,text) {
    const n=document.createElementNS('http://www.w3.org/2000/svg',tag);
    for(const [k,v] of Object.entries(attrs)) n.setAttribute(k,v);
    if(text!==undefined)n.textContent=text;
    parent.append(n);return n;
  }
  const rectangle=(g,x,y,w,h,fill,stroke='none',cls='')=>make('rect',{x,y,width:w,height:h,rx:8,fill,stroke,class:cls},g);
  const text=(g,x,y,str,size=11,color='#e3f4ed',anchor='start')=>make('text',{
    x,y,fill:color,'text-anchor':anchor,'font-size':size,'font-family':'ui-monospace,monospace'},g,str);
  const circle=(g,x,y,r,fill,stroke)=>make('circle',{cx:x,cy:y,r,fill,stroke},g);
  function action(node,label,fn) {
    node.setAttribute('role','button');node.setAttribute('tabindex','0');node.setAttribute('aria-label',label);
    node.onclick=fn;
    node.addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();fn();}});
  }
  function label(c,id,x,y,name,mark='right') {
    const p={c,x,y,name,mark}; pins[`${c}.${id}`]=p;return `${c}.${id}`;
  }
  function wire(id,from,to,group,color,signal,info,via=[],form=null) {
    wires.push({id,from,to,group,color,signal,info,via,form,optional:form==='led'||form==='split',
      level:color==='ground'?'GND':'3.3 V'});
  }
  const pins={}, wires=[], keyPlacements=[];
  const buttonColumns=[4,10,16,21];
  const buttonReturns=[6,12,18,22];
  const footprints={compact2:2,compact1:1,across:0};
  const footprint=()=>$('solo-footprint').value;
  const hostKeys=['SCK','MOSI','TFT_CS','TFT_DC','TFT_RESET','UP','DOWN','MENU','SELECT','SD_MISO','SD_CS'];
  hostKeys.forEach((k,i)=>label('esp',k,327,187+i*25,`GPIO${cfg[k]} · ${k.replaceAll('_',' ')}`,'left'));
  label('esp','GND',112,468,'GND','above');
  label('esp','VCC',234,468,'3V3 OUT','above');
  const rail=(id,i,gnd)=>label('rail',id,half.railCol(i),gnd?half.gnd:half.vcc,
    `${gnd?'−':'+'}${i+1} (${gnd?'GND':'3V3'})`,'none');
  const host3V3=rail('host-power',4,false);
  const hostGround=rail('esp-gnd',1,true);
  wire('esp-gnd','esp.GND',hostGround,'power','ground','ESP32 ground',
    'Join the ESP32 ground to the half-size board ground rail.',[[112,half.gnd]]);
  wire('dev-power','esp.VCC',host3V3,'power','power','3V3 output → rail',
    'USB development board only: use a documented 3.3 V output if its regulator can supply all attached peripherals. USB feeds the board; do not feed this pin from another supply.',[[234,half.vcc]],'dev');
  for(const gnd of [false,true]) {
    const kind=gnd?'ground':'power', y=gnd?half.gnd:half.vcc, sign=gnd?'−':'+';
    wire(`bridge-${kind}`,rail(`bridge-${kind}-left`,8,gnd),rail(`bridge-${kind}-right`,16,gnd),
      'power',kind,`${sign} rail midpoint bridge`, 'Use only after verifying the two halves of this rail are isolated.',
      [[half.railCol(8),y+7],[half.railCol(16),y+7]],'split');
  }
  for(const [i,[key,signal,color,info]] of [
    ['SCK','SCK / SCL','spi','GPIO drives SPI clock.'],
    ['MOSI','MOSI / SDA','data','GPIO drives TFT data in. Verify the TFT pin is SPI MOSI, not I²C SDA.'],
    ['TFT_CS','CS','cs','Active-low TFT chip select. The firmware sets it high before starting SPI.'],
    ['TFT_DC','DC / RS','spi','Data/command selection.'],
    ['TFT_RESET','RST / RESET','reset','Active-low TFT reset.']
  ].entries()) {
    const y=153+23*i, from=`esp.${key}`, to=label('tft',key,704,y,signal), col=477+i*28;
    wire(`tft-${key}`,from,to,'tft',color,signal,info,[[col,pins[from].y],[col,y]]);
  }
  const panelVcc=label('tft','VCC',869,291,'VCC','above'),panelGnd=label('tft','GND',834,291,'GND','above');
  const pv=rail('tft-pv',18,false),pg=rail('tft-pg',19,true);
  wire('tft-vcc',pv,panelVcc,'tft','power','VCC (conditional)',
    'Regulated 3.3 V only after verifying the breakout accepts it. Never assume VCC is 5 V tolerant.',
    [[pins[pv].x,729],[1033,729],[1033,304],[869,304]]);
  wire('tft-gnd',panelGnd,pg,'tft','ground','Ground','Use the half-size board’s common GND rail.',
    [[834,299],[1017,299],[1017,754],[pins[pg].x,754]]);
  const ledPin=label('tft','LED',922,291,'LED?','above'), ledPower=rail('led-feed',21,false);
  wire('tft-led',ledPower,ledPin,'tft','power','Backlight 3.3 V (conditional)',
    'Only for a separate LED/BL supply pin explicitly rated for direct 3.3 V with current limiting on the breakout. Otherwise omit and follow the module’s backlight specification. This is not connected to a GPIO.',
     [[pins[ledPower].x,716],[1062,716],[1062,312],[922,312]],'led');
  const sdPins={
    SCK:label('sd','SCK',704,337,'SCK','left'),
    MOSI:label('sd','MOSI',704,356,'MOSI / DI','left'),
    MISO:label('sd','MISO',704,375,'MISO / DO','left'),
    CS:label('sd','CS',704,394,'CS','left'),
    VCC:label('sd','VCC',954,413,'3V3','right'),
    GND:label('sd','GND',954,432,'GND','right')
  };
  for(const [id,from,to,color,signal,info,lane] of [
    ['sd-SCK','esp.SCK',sdPins.SCK,'spi','SPI SCK · shared',
       'Shared SPI clock for TFT and microSD. Both devices need their own chip-select signal.',610],
    ['sd-MOSI','esp.MOSI',sdPins.MOSI,'data','SPI MOSI · shared',
       'Shared SPI data from ESP32 to TFT and microSD.',630],
    ['sd-MISO',sdPins.MISO,'esp.SD_MISO','data','SPI MISO · card to ESP32',
       'GPIO19 is the return-data line for the SD card. The write-only TFT does not use MISO.',650],
    ['sd-CS','esp.SD_CS',sdPins.CS,'cs','SD card CS · GPIO27',
       'Independent chip-select for the SD card. Keep TFT chip-select on its separate GPIO22 line.',670]
  ]) {
    wire(id,from,to,'sd',color,signal,info,[[lane,pins[from].y],[lane,pins[to].y]]);
  }
  wire('sd-vcc',host3V3,sdPins.VCC,'sd','power','3.3 V → SD VCC',
    'Verify that the exact module accepts 3.3 V and that the board regulator has enough current for all attached devices.',
    [[pins[host3V3].x,715],[1030,715],[1030,413],[954,413]]);
  wire('sd-gnd',hostGround,sdPins.GND,'sd','ground','Common GND → SD GND',
    'Use the ESP32 board common ground. Disconnect USB before changing wiring.',
    [[pins[hostGround].x,755],[1050,755],[1050,432],[954,432]]);
  for(const [i,key] of ['UP','DOWN','MENU','SELECT'].entries()) {
    const col=buttonColumns[i], x=half.col(col-1), host=`esp.${key}`;
    const feed=rail(`${key}-return`,buttonReturns[i]-1,true);
    for(const [layout,delta] of Object.entries(footprints)) {
      const id=key.toLowerCase()+(layout==='compact2'?'':`-${layout}`);
      const acc=col+delta, rx=half.col(acc-1), y1=half.top[4], y2=delta?y1:half.bottom[0];
      const w=28,h=26, cx=(x+rx)/2, y=delta?y1-33:(y1+y2-h)/2;
      const first=`E${col}`,second=delta?`E${acc}`:`F${col}`, retHole=delta?`A${acc}`:`J${col}`;
      parts[id]={title:key,x:cx-w/2,y,w,h,fill:'#395d4d',contacts:[[x,y1],[rx,y2]],first,second,
        info:`GPIO${cfg[key]} → A${col}. Place the normally open contacts in ${first} and ${second}, then jumper ${retHole} to −${buttonReturns[i]}. Never land both contacts in the same connected strip.`};
      const input=label(id,'IN',x,half.top[0],`Half-size A${col}`,'none');
      const ret=label(id,'GND',rx,delta?half.top[0]:half.bottom[4],`Half-size ${retHole}`,'none');
      const laneX=341+10*i, laneY=414+8*i;
      wire(`key-${id}`,host,input,'buttons','button',key,parts[id].info,
        [[laneX,pins[host].y],[laneX,laneY],[x,laneY]],layout);
      const low=half.bottom[4]+10, bendX=delta?rx+8:rx;
      wire(`return-${id}`,ret,feed,'buttons','ground',`${key} return`,
        `Jumper the return strip ${retHole} to common GND at −${buttonReturns[i]}.`,
        [[bendX,pins[ret].y],[bendX,low],[pins[feed].x,low]],layout);
      keyPlacements.push({id,key,gpio:cfg[key],col,first,second,retHole,railHole:buttonReturns[i],layout});
    }
  }

  let mode='all', chosen='', part='', hover='', phoneMode=false, activeStep=-1;
  const body=$('solo-connections'), boardNodes={}, pinNodes={}, wireNodes={}, rows={};
  const defs=make('defs',{},svg), grid=make('pattern',{id:'solo-grid',width:20,height:20,patternUnits:'userSpaceOnUse'},defs);
  make('path',{d:'M20 0H0V20',fill:'none',stroke:'#293d47','stroke-width':.7},grid);
  make('rect',{width:1080,height:779,fill:'url(#solo-grid)'},svg);
  const mapTitle=text(svg,29,25,'VERSION B · LOGICAL GPIO MAP · NOT CARRIER HEADER ORDER',12,'#9cbcaf');
  const boardLayer=make('g',{},svg), wireLayer=make('g',{},svg), pinLayer=make('g',{},svg);
  let sourceCopy;
  for(const [id,p] of Object.entries(parts)) {
    const {x,y,w,h,fill}=p, g=make('g',{class:'component','data-solo-part':id},boardLayer);
    boardNodes[id]=g;
    action(g,p.title,()=>{part=part===id?'':id;chosen=hover='';focus();});
    if(id==='rail') {
      SNTCH_BREADBOARDS.draw(g,half,[],[]);
      continue;
    }
    if(p.contacts) {
      const [[a,e],[b,f]]=p.contacts, cx=x+w/2, same=e===f;
      make('path',{d:`M${a},${e}V${same?y+h:y}H${same?cx-9:cx} M${b},${f}V${y+h}H${same?cx+9:cx}`,
        fill:'none',stroke:'#567d63','stroke-width':2},g);
      circle(g,a,e,3,'#abc995','#335747'); circle(g,b,f,3,'#b5c4c8','#335747');
      make('title',{},g,`Small ${p.title} pushbutton · contacts ${p.first} and ${p.second} · verify pin spacing`);
    }
    rectangle(g,x,y,w,h,fill,'#71978b','outline');
    if(id==='source') {
      text(g,x+12,y+21,p.title,15);
      sourceCopy=text(g,x+111,y+49,'MICRO-USB · POWER + FLASH',9,'#ffdb9e');
    } else if(id==='esp') {
      rectangle(g,x+3,y+4,w-6,h-8,'#174d44','#94b4a3');
      for(let i=0;i<19;i++){
        const yy=y+24+i*13.9;
        circle(g,x+9,yy,3,'#a6b2ad','#e6ece6');
        circle(g,x+w-9,yy,3,'#a6b2ad','#e6ece6');
      }
      rectangle(g,x+39,y+16,w-78,133,'#aeb8b8','#e0e8e6');
      text(g,x+w/2,y+48,'ESP-32U',16,'#1e302e','middle');
      text(g,x+w/2,y+66,'38-PIN DEV BOARD',8,'#1e302e','middle');
      rectangle(g,x+w-93,y+28,15,15,'#d1c08f','#f1dfa8');
      circle(g,x+w-85.5,y+35.5,3.5,'#283b3c','#eed38a');
      text(g,x+w-85,y+56,'U.FL',7,'#1e302e','middle');
      text(g,x+w/2,y+129,'external antenna required',7,'#1e302e','middle');
      rectangle(g,x+42,y+169,51,36,'#233b35','#91aa9c');
      text(g,x+67,y+184,'USB-UART',7,'#ecf6ef','middle');
      text(g,x+67,y+195,'bridge*',7,'#ecf6ef','middle');
      rectangle(g,x+104,y+169,41,36,'#32473e','#a2b9aa');
      text(g,x+124.5,y+184,'3V3',8,'#ecf6ef','middle');
      text(g,x+124.5,y+195,'reg.*',7,'#ecf6ef','middle');
      text(g,x+w/2,y+219,'*IC MARKINGS UNREADABLE',7,'#d2e3d9','middle');
      rectangle(g,x+58,y+h-73,30,18,'#344d42','#b6c9bb');
      rectangle(g,x+w-88,y+h-73,34,18,'#344d42','#b6c9bb');
      text(g,x+73,y+h-60,'EN',7,'#f3fff7','middle');
      text(g,x+w-71,y+h-60,'BOOT',7,'#f3fff7','middle');
      rectangle(g,x+w/2-27,y+h-46,54,30,'#9da9ab','#e3e8e8');
      rectangle(g,x+w/2-16,y+h-40,32,18,'#26323a','#718286');
      text(g,x+w/2,y+h-7,'MICRO-USB · 5 V',8);
      text(g,x+21,y+252,'38-pin header · see GPIO label map',8);
    } else if(id==='tft') {
      text(g,x+125,y+17,p.title,13,'#edf6f3','middle');
      rectangle(g,x+122,y+38,112,98,'#111921','#78a394');
      make('image',{href:'mascot.svg',x:x+147,y:y+51,width:63,height:65},g);
    } else if(id==='sd') {
      rectangle(g,x+28,y+14,w-56,h-28,'#aebbbc','#e3ece9');
      rectangle(g,x+49,y+34,47,66,'#28383a','#526a67');
      rectangle(g,x+57,y+37,31,44,'#8ea2a0','#d7dfdc');
      text(g,x+151,y+54,'SPI microSD',15,'#1e302e','middle');
      text(g,x+151,y+74,'REQUIRED FOR OPTION C',8,'#1e302e','middle');
      text(g,x+151,y+91,'VERIFY MODULE VOLTAGE',8,'#1e302e','middle');
      make('title',{},g,'External SPI microSD module for Option C classroom storage.');
    } else {
      circle(g,x+w/2,y+h/2,8,p.title==='SELECT'?'#378969':'#203b31','#a1c4b1');
      if(p.contacts[0][1]===p.contacts[1][1])
        text(g,x+w/2,half.y+121,p.title,9,'#253d33','middle');
      else text(g,x+w+5,y+h/2+4,p.title,9,'#253d33');
    }
  }
  const endpoint=id=>`${parts[pins[id].c].title} / ${pins[id].name}`;
  const statusBadge=s=>{
    const el=element('span',s);el.className=`badge ${s}`;return el;
  };
  for(const w of wires) {
    const g=make('g',{class:'wire','data-solo-wire':w.id},wireLayer);
    wireNodes[w.id]=g;
    const a=pins[w.from],b=pins[w.to];
    const d=`M${a.x},${a.y}`+[...w.via,[b.x,b.y]].map(([x,y])=>`L${x},${y}`).join('');
    make('path',{class:'lead',d,stroke:palette[w.color],'stroke-dasharray':w.optional?'6 5':'none'},g);
    make('path',{class:'hit',d},g);
    make('title',{},g,`${endpoint(w.from)} → ${endpoint(w.to)}`);
    action(g,w.signal,()=>pick(w.id));
    g.addEventListener('mouseenter',()=>{hover=w.id;focus();});
    g.addEventListener('mouseleave',()=>{hover='';focus();});
  }
  for(const [id,p] of Object.entries(pins)) {
    const n=make('g',{class:'pin'},pinLayer);
    pinNodes[id]=n;
    circle(n,p.x,p.y,4,'#17252b','#d6e5dc');
    if(p.mark!=='none') {
      const dx=p.mark==='left'?-9:p.mark==='right'?9:0;
      const dy=p.mark==='above'?-7:p.mark==='below'?12:4;
      text(n,p.x+dx,p.y+dy,p.name,p.c==='esp'?10:9,'#e3f4ed',dx<0?'end':dx?'start':'middle');
    }
    n.onclick=()=>{
      const ws=visible().filter(w=>w.from===id||w.to===id);
      if(ws.length===1) pick(ws[0].id);
      else {part=p.c;chosen=hover='';focus();}
    };
  }

  const form=()=>$('solo-form').value, tft=()=>$('solo-tft').value;
  const verifiedRail=()=>$('solo-rail').checked;
  function status(w) {
    if(form()==='blocked' || (w.group==='tft'&&tft()==='blocked')) return 'invalid';
    if(w.group==='sd') return 'unverified';
    if(!form() || (w.group==='tft'&&tft()!=='ok') ||
       (w.color==='power'&&!verifiedRail())) return 'unverified';
    return 'proposed';
  }
  function visible() {
    const optionOn={split:$('solo-split').checked,led:$('solo-led').checked,
      compact2:footprint()==='compact2',compact1:footprint()==='compact1',across:footprint()==='across'};
    return wires.filter(w=>(phoneMode?w.group!=='buttons':w.group!=='sd')&&
      (!w.form||(w.form in optionOn?optionOn[w.form]:w.form===form()))&&(mode==='all'||mode===w.group||
      (mode==='buttons'&&w.id==='esp-gnd')||
      (mode==='sd'&&['esp-gnd','dev-power'].includes(w.id))||
      (mode==='power'&&['power','ground'].includes(w.color))));
  }
  function focus() {
    const list=visible(), active=hover||chosen, ids=new Set(list.map(w=>w.id));
    const boardIds=new Set(list.flatMap(w=>[pins[w.from].c,pins[w.to].c]));
    if(mode==='all'||mode==='power'||mode==='sd')boardIds.add('source');
    const ps=new Set(list.flatMap(w=>[w.from,w.to]));
    const selected=list.find(w=>w.id===active);
    const hits=list.filter(w=>active?w.id===active:part&&(pins[w.from].c===part||pins[w.to].c===part));
    const highlighted=new Set(hits.flatMap(w=>[w.from,w.to]));
    for(const [id,n] of Object.entries(boardNodes)) n.classList.toggle('hidden',!boardIds.has(id));
    for(const [id,n] of Object.entries(pinNodes)) {
      n.style.display=ps.has(id)?'':'none';
      n.classList.toggle('active',highlighted.has(id));
    }
    for(const w of wires) {
      const n=wireNodes[w.id], hit=hits.includes(w);
      n.classList.toggle('hidden',!ids.has(w.id));
      n.classList.toggle('quiet',!!(active||part)&&!hit);
      n.classList.toggle('active',hit);
      n.classList.toggle('blocked',status(w)==='invalid');
      rows[w.id]?.classList.toggle('active',w.id===active);
    }
    const p=parts[part];
    $('solo-inspect-title').textContent=selected?.signal || p?.title || 'Pick a part or wire';
    $('solo-inspect-info').textContent=selected?
      `${endpoint(selected.from)} → ${endpoint(selected.to)} · ${selected.level} · ${status(selected)}. ${selected.info}`:
      part==='source'?(phoneMode?phoneSteps[0][2]:steps[0][2]):
      p?.info || 'The ESP32 pin positions are schematic. Match GPIO numbers against the documentation for your board.';
  }
  function pick(id) {chosen=chosen===id?'':id;part=hover='';focus();}
  function refresh() {
    svg.classList.toggle('split-rails',$('solo-split').checked);
    const items=[
      [form(), 'Confirm this photo-identified board exposes each labeled GPIO.',
        'The board form is photo-matched; verify the actual silkscreen and peripheral power before wiring.',
        'Do not wire this reference: unsupported carrier or missing GPIOs.'],
      [tft(),'TFT supply, SPI and LED requirements still need verification.',
        'TFT requirements marked verified. No 5 V TFT signals in this design.',
        'The selected TFT cannot use the 3.3 V reference wiring.'],
      [verifiedRail()?'ok':'','Check regulator capacity with all intended loads; verify 3.3 V under load.',
        'Supply capacity marked verified. Never tie together two powered 3.3 V sources.',''],
      ...(phoneMode?[['','Verify the microSD module’s 3.3 V power and logic requirements and test with a prepared card.','','']]:[])
    ];
    $('solo-checks').replaceChildren(...items.map(([v,unknown,good,bad])=>{
      const li=element('li');
      li.append(statusBadge(v==='blocked'?'invalid':v?'confirmed':'unverified'),
        document.createTextNode(` ${v==='blocked'?bad:v?good:unknown}`));
      return li;
    }));
    sourceCopy.textContent='MICRO-USB · POWER + FLASH';
    const list=visible();
    $('solo-wire-count').textContent=`· ${list.length} segments`;
    for(const id of Object.keys(rows)) delete rows[id];
    body.replaceChildren(...list.map(w=>{
      const row=element('tr');row.dataset.soloWire=w.id;rows[w.id]=row;
      const pinSelect=element('button',endpoint(w.from));
      pinSelect.onclick=e=>{e.stopPropagation();pick(w.id);};
      for(const cell of [pinSelect,endpoint(w.to),w.signal,w.level,statusBadge(status(w))]) {
        const td=element('td');td.append(cell);row.append(td);
      }
      row.onclick=()=>pick(w.id);
      return row;
    }));
    focus();
  }
  function view(key,step=-1) {
    mode=key;activeStep=step;part=chosen=hover='';
    const selectedSteps=phoneMode?phoneSteps:steps;
    for(const n of $('solo-modes').children)n.setAttribute('aria-pressed',n.dataset.mode===key);
    $('solo-steps').querySelectorAll('button').forEach((n,i)=>n.setAttribute('aria-pressed',step===i));
    $('solo-step-info').textContent=selectedSteps[step]?.[2]||`Start with power and ground, then connect the TFT${phoneMode?' and microSD module.':' and four buttons.'}`;
    refresh();
  }
  for(const [k,name] of Object.entries(modes)) {
    const b=element('button',name);b.dataset.mode=k;b.onclick=()=>view(k);$('solo-modes').append(b);
  }
  function renderSteps() {
    const chosenSteps=phoneMode?phoneSteps:steps;
    $('solo-steps').replaceChildren(...chosenSteps.map(([key,name],index)=>{
      const li=element('li'),button=element('button',name);
      button.setAttribute('aria-pressed',index===activeStep);
      button.onclick=()=>view(key,index);
      li.append(button);
      return li;
    }));
  }
  function showKeyPlacements() {
    $('solo-placement-keys').replaceChildren(...keyPlacements.filter(k=>k.layout===footprint()).map(k=>{
      const {id,key,gpio,col,first,second,retHole,railHole}=k, row=element('tr'), b=element('button',`${key} · Half-size`);
      b.onclick=()=>{view('buttons');part=id;focus();$('solo-diagram').scrollIntoView({block:'nearest'});};
      for(const item of [b,`GPIO${gpio} → A${col}`,`${first} and ${second}`,`${retHole} → −${railHole}`]) {
        const td=element('td');td.append(item);row.append(td);
      }
      return row;
    }));
  }
  $('solo-footprint').onchange=()=>{part=chosen=hover='';showKeyPlacements();refresh();};
  for(const [id,side,condition] of [
    ['dev-power','to','Board 3V3 output: confirm current capacity for attached TFT and logic'],
    ['esp-gnd','to','Common ESP32 development-board ground'],
    ['tft-vcc','from','Only after verifying TFT VCC accepts 3.3 V'],
    ['tft-gnd','to','TFT ground'],
    ['tft-led','from','Optional: only for a separate LED/BL power input rated for direct 3.3 V with onboard current limiting']
  ]) {
    const w=wires.find(w=>w.id===id), row=element('tr');
    for(const item of [w.signal,pins[w[side]].name,condition])row.append(element('td',item));
    $('solo-placement-power').append(row);
  }
  for(const id of ['solo-form','solo-tft','solo-rail','solo-split','solo-led']) $(id).addEventListener('change',refresh);
  function architecture() {
    const buttonPanel=['#esp32-only','#breadboard-placement'].includes(location.hash);
    const phone=!buttonPanel&&location.hash!=='#dual-controller';
    const solo=buttonPanel||phone;
    phoneMode=phone;
    $('dual-build').hidden=solo; $('solo-build').hidden=!solo;
    $('choose-dual').setAttribute('aria-pressed',!solo);$('choose-solo').setAttribute('aria-pressed',solo&&!phone);
    $('choose-phone').setAttribute('aria-pressed',phone);
    $('solo-buttons-inventory').hidden=phone;
    $('solo-phone-inventory').hidden=!phone;
    $('phone-admin-guide').hidden=!phone;
    $('breadboard-placement').hidden=phone;
    $('solo-placement-jump').hidden=phone;
    $('solo-modes').setAttribute('aria-label',phone?'Phone-admin option wiring views':'ESP32-only button-panel wiring views');
    $('solo-notice-title').textContent=phone?'Option C · default build':'Alternate profile only';
    $('solo-notice-copy').textContent=phone
      ?'Connect an external SPI microSD module and insert a prepared FAT32 card. Once storage is ready, the controller starts its local Wi-Fi network and shows the connection QR.'
      :'Version B moves the TFT and buttons to ESP32 GPIO and is not the selected UNO architecture. It still needs the external antenna and verified board/TFT power. The same photo-identified board pin labels apply only where the required GPIOs are exposed.';
    $('solo-heading').textContent=phone?'Option C · ESP32 + TFT + microSD':'Version B · ESP32-only wiring';
    $('solo-connections-label').textContent=phone?'OPTION C · PIN REGISTER':'ESP32-ONLY PIN REGISTER';
    $('solo-connection-note').textContent=phone
      ?'The TFT shares SPI clock and MOSI with the card. The card uses its own CS on GPIO27 and returns data on GPIO19.'
      :'The TFT uses write-only SPI. Leave MISO and any microSD pins disconnected.';
    mapTitle.textContent=phone
      ?'OPTION C · AUTO-START / PHONE ADMIN · LOGICAL GPIO MAP'
      :'VERSION B · LOGICAL GPIO MAP · NOT CARRIER HEADER ORDER';
    $('solo-diagram-title').textContent=phone
      ?'Automatic-start ESP32, TFT, and microSD SPI wiring without physical control buttons'
      :'ESP32-only wiring and button placement on one half-size breadboard';
    $('solo-modes').querySelectorAll('button').forEach(button=>{
      button.hidden=(phone&&button.dataset.mode==='buttons')||(!phone&&button.dataset.mode==='sd');
    });
    if((phone&&mode==='buttons')||(!phone&&mode==='sd')) mode='all';
    activeStep=-1;
    renderSteps();
    $('solo-modes').querySelectorAll('button').forEach(button=>{
      button.setAttribute('aria-pressed',button.dataset.mode===mode);
    });
    if(phone) $('solo-diagram').setAttribute('data-phone-admin','true');
    else $('solo-diagram').removeAttribute('data-phone-admin');
    view(mode);
  }
  $('choose-dual').onclick=()=>{location.hash='dual-controller';architecture();};
  $('choose-solo').onclick=()=>{location.hash='esp32-only';architecture();};
  $('choose-phone').onclick=()=>{location.hash='phone-admin';architecture();};
  window.addEventListener('hashchange',architecture);
  showKeyPlacements();architecture();
})();

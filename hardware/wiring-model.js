/* Geometry and netlist. Controller pin numbers come from config.js. */
const SNTCH_WIRING = (() => {
  const {uno,esp}=SNTCH_CONFIG;
  const boards=SNTCH_BREADBOARDS.dual;
  const colors={ground:['#c5cdd3','Gray'],p5:['#f27778','Red'],p3:['#fbab59','Orange'],
    clock:['#36cf9c','Mint'],data:['#f2d379','Yellow'],cs:['#d394f0','Lilac'],rf:['#e3c875','RF coax'],
    dc:['#64e1db','Teal'],rst:['#f994bc','Pink'],tx:['#6cb6f2','Blue'],rx:['#e1abff','Violet'],
    button:['#a9dc72','Lime'],boot:['#93adcb','Slate']};
  const parts={}, pins={}, wires=[];
  function part(id,title,x,y,w,h,role,power) {
    parts[id]={id,title,x,y,w,h,role,power};
  }
  function pin(c,id,label,x,y,kind='passive',v=0,align='start',dy=4,allowed=true) {
    pins[`${c}.${id}`]={key:`${c}.${id}`,c,id,label,x,y,kind,v,align,dy,allowed};
  }
  function wire(id,from,to,signal,volts,color,step,groups,notes,via=[],option='',local=false) {
    if(!pins[from]||!pins[to]) throw Error(`Unknown endpoint: ${from} → ${to}`);
    wires.push({id,from,to,signal,volts,color,step,groups,notes,via,option,local});
  }
  part('uno','Arduino Board Model UNO R3',59,137,337,271,'TFT, mascot, menus, button debounce and holds; UART requester.','Power through USB-B. Its 5V header powers only U2 in this design; never connect it to ESP32 GPIO or 3V3.');
  part('lvc','U1 · 74LVC245',507,133,184,200,'Converts UNO SPI/control and UART TX to 3.3 V. Set DIR high for A→B and /OE low.','Power VCC from verified board 3V3 output. Confirm the exact part tolerates 5 V inputs at 3.3 V VCC. Use an SOIC-20 adapter; fit 100 nF at VCC/GND.');
  part('tft','1.8 TFT SPI 128x160 v1.1',770,119,212,223,'128×160 panel used at 160×128 in landscape. The firmware is set to ST7735 BLACKTAB; confirm your driver and init tab. Pins show signal names and common aliases, not a verified header order.','Check the actual VCC rating and LED pin before connecting. An LED power pin is not the optional BL_EN logic input.');
  part('esp','ESP-32U development board · 38-pin',1083,119,179,352,
    'Photo-identified development board: ESP-32U marking, two 19-pin headers, micro-USB, EN and BOOT buttons, and an IPEX/U.FL-style antenna socket. Exact SKU and most header labels are not readable. GPIO16/RX2 and GPIO17/TX2 are the required logical UART labels; the drawn pin positions are schematic, not a verified header order.',
    'Power the board through its micro-USB port. The onboard regulator and USB-UART bridge are visible, but their part numbers and current/flash ratings are unreadable. Do not inject external 3.3 V into the board while USB is connected.');
  part('antenna','2.4 GHz Wi-Fi antenna',1282,526,100,104,
    'Required by the photographed board’s external-antenna socket. Use a 2.4 GHz Wi-Fi antenna with a 50 Ω feed and the exact mating connector series and gender for the socket. The photo shows a U.FL-style socket, but does not resolve its exact series or the antenna.');
  part('usbhost','USB data / power source',1084,535,174,78,
    'Micro-USB provides board power and the onboard USB-to-UART programming link. Use a known-good data cable and a regulated USB 5 V source. Do not connect the UNO 5 V rail to the ESP32 5 V/3V3 pins.');
  part('upshift','U2 · 74HCT125D',742,365,204,119,'One 74HCT125D buffer gate up-shifts ESP32 TX2. HCT TTL input thresholds accept a 3.3 V high; the 5 V output drives UNO RX. Enable the used gate by tying /OE low.','Power VCC from UNO 5 V and share GND. Confirm the exact part is HCT (not HC) and its datasheet accepts 3.3 V logic high. Tie unused inputs low, disable unused gates by tying their /OE pins high, and leave their outputs open. Use an SOIC-14 adapter; fit 100 nF at VCC/GND.');
  const partBoard=(id,title,b,role,power)=>part(id,title,b.x,b.y,b.w,b.h,role,power);
  partBoard('bread','Half-size breadboard',boards.half,
    'UP and DOWN sit across the center trench if their legs fit; otherwise use jumpers. Upper and lower groups are isolated. Verify continuity on the power rails; bridge any intended split section.',
    'Use the development board’s regulated 3V3 output for translator logic only after confirming its current headroom; TFT power from that rail is conditional on the board and TFT ratings. UNO, ESP32 board, translators and TFT share GND, but their positive rails stay separate.');
  partBoard('mini','Mini breadboard',boards.mini,
    'MENU and SELECT use separate inputs. Bring both switch returns and the half-board GND jumper to three different holes in the same shaded lower five-hole strip (illustrated column 8).',
    'No power rail assumed. No positive supply jumper is needed for these switches.');
  const ds={};
  for(const [i,n] of [13,12,11,10,9,8,7,6,5,4,3,2,1,0].entries()) {
    ds[n]=127+i*17;
    const key=({[uno.TFT_SCK]:'SCK',[uno.TFT_MOSI]:'MOSI',[uno.TFT_CS]:'CS',[uno.TFT_DC]:'D/C',[uno.TFT_RESET]:'RST'})[n];
    pin('uno',`D${n}`,`D${n}${key?' '+key:''}`,ds[n],151,key||n===uno.BACKLIGHT?'out':'io',5,'vertical',0,
      ![0,1,7,12].includes(n));
  }
  for(let i=0;i<6;i++) pin('uno',`A${i}`,`A${i}${`A${i}`===uno.UART_RX?' RX':`A${i}`===uno.UART_TX?' TX':''}`,
    282+i*17,390,`A${i}`===uno.UART_TX?'tx':`A${i}`===uno.UART_RX?'rx':'io',5,'vertical-bottom',0,i<2);
  pin('uno','5V','5V out',178,390,'supply',5,'middle',-10);
  pin('uno','GND','GND',222,390,'ground',0,'middle',-10);
  const spi=[
    ['sck',uno.TFT_SCK,'SCK',2,18,'clock'],['mosi',uno.TFT_MOSI,'MOSI',3,17,'data'],
    ['cs',uno.TFT_CS,'CS',4,16,'cs'],['dc',uno.TFT_DC,'DC',5,15,'dc'],
    ['reset',uno.TFT_RESET,'RST',6,14,'rst'],['uart',uno.UART_TX,'RX2',7,13,'tx'],
    ['bl',uno.BACKLIGHT,'BL_EN',8,12,'rst']];
  spi.forEach(([id,n,label,a,b,color],i)=>{
    pin('lvc',`A${i}`,`A · ${a}`,510,168+i*20,'in',5);
    pin('lvc',`B${i}`,`B · ${b}`,688,168+i*20,'out',3.3,'end');
    const opt=id==='bl'?'bl':'', isUart=id==='uart', group=isUart?['uart']:['tft'], step=isUart?5:3;
    const lead=typeof n==='number'?`D${n}`:n;
    wire(`${id}a`,`uno.${lead}`,`lvc.A${i}`,isUart?'UNO TX → U1':label,5,color,step,group,
       opt?'Optional logic EN only; if unused, tie U1 A7 input pin 8 low and leave B7 pin 12 open.':'Use a verified 5 V-tolerant U1 input; never bypass the buffer.',
      isUart?[[424,441],[478,441],[478,168+i*20]]:[[ds[n],54+i*8],[459+i*6,54+i*8],[459+i*6,168+i*20]],opt);
    const py=168+i*20;
    if (!isUart) {
      const ty=147+(i===6?5:i)*22;
      const legend={SCK:'SCK/SCL',MOSI:'MOSI/SDA',DC:'DC/RS',BL_EN:'BL_EN?'};
      pin('tft',label,legend[label]??label,772,ty,'in',3.3);
      wire(`${id}b`,`lvc.B${i}`,`tft.${label}`,label,3.3,color,step,group,
        opt?'Only an active-high logic input. Do not drive LED+/LED− here.':'Keep short. Verify the breakout pin marked MOSI (sometimes SDA) is SPI data in.',[[714+i*5,py],[714+i*5,ty]],opt);
    }
  });
  pin('lvc','VCC','VCC · 20',546,332,'power',3.3,'middle',-9);
  pin('lvc','GND','GND · 10',608,332,'ground',0,'middle',-9);
  pin('lvc','DIR','DIR · 1',648,332,'in',3.3,'middle',14);
  pin('lvc','OE','/OE · 19',690,332,'in',3.3,'middle',14);
  pin('lvc','UNUSED','A8 · 9',511,319,'in',3.3,'start',-6);
  pin('tft','VCC','VCC ?',772,290,'power',3.3);
  pin('tft','GND','GND',772,316,'ground');
  const left=['3V3','EN','VP / GPIO36','VN / GPIO39','GPIO34','GPIO35','GPIO32','GPIO33','GPIO25','GPIO26','GPIO27','GPIO14','GPIO12','GND','GPIO13','SD2 / GPIO9','SD3 / GPIO10','CMD / GPIO11','5V'];
  const right=['GND','GPIO23','GPIO22','TX0 / GPIO1','RX0 / GPIO3','GPIO21','GND','GPIO19','GPIO18','GPIO5','GPIO17','GPIO16','GPIO4','GPIO0','GPIO2','GPIO15','SD1 / GPIO8','SD0 / GPIO7','CLK / GPIO6'];
  const espHeader={gnd:'esp.L14',v3:'esp.L1',rx:'esp.R12',tx:'esp.R11'};
  for(const [side,list] of [[0,left],[1,right]]) list.forEach((label,i)=>{
    const gpio=label.match(/GPIO(\d+)/)?.[1];
    const kind=label==='GND'?'ground':label==='3V3'||label==='5V'?'supply':
      label.includes('RX0')||label===`GPIO${esp.UART_RX}`?'rx':
      label.includes('TX0')||label===`GPIO${esp.UART_TX}`?'tx':
      label==='EN'||label==='GPIO0'?'in':'io';
    const forbidden=/GPIO(?:[6-9]|1[01])$/.test(label);
    pin('esp',`${side?'R':'L'}${i+1}`,`${side?'R':'L'}${i+1} · ${label}`,
      side?1255:1090,169+i*14.15,kind,label==='GND'?0:label==='5V'?5:3.3,
      side?'start':'end',3,!forbidden);
  });
  pin('esp','USB','micro-USB',1172,468,'supply',5,'middle',-9);
  pin('esp','ANT','U.FL RF',1219,158,'rf',0,'start',-7);
  pin('usbhost','PORT','USB data cable',1138,574,'supply',5,'middle',-10);
  pin('antenna','RF','50 Ω coax',1374,585,'rf',0,'end',4);
  wire('usb-link','usbhost.PORT','esp.USB','ESP32 micro-USB · power + programming',5,'p5',2,['power'],
    'Use a known-good USB data cable and a separate USB source. This powers/programs the ESP32 board; it is not a jumper from the UNO 5 V rail.',
    [[1115,620],[1060,620],[1060,490],[1172,490]]);
  wire('rf-coax','esp.ANT','antenna.RF','2.4 GHz · 50 Ω antenna coax',0,'rf',2,['rf'],
    'Connect only a 50 Ω 2.4 GHz antenna with the exact mating connector. The photos do not confirm the connector series.',
    [[1230,158],[1370,158],[1370,585]]);
  wire('uartb','lvc.B5',espHeader.rx,'UNO TX → ESP GPIO16/RX2',3.3,'tx',5,['uart'],
    'UART2 receive at the board header labelled 16. The signal leaves U1 at 3.3 V.',[[723,268],[723,82],[1329,82],[1329,pins[espHeader.rx].y]]);
  pin('upshift','A','A · 2',944,392,'in',5,'end');
  pin('upshift','Y','Y · 3',743,392,'out',5);
  pin('upshift','GND','GND · 7',777,482,'ground',0,'middle',-10);
  pin('upshift','OE','/OE · 1',944,444,'in',5,'end');
  pin('upshift','VCC','VCC · 14',743,444,'power',5);
  wire('uartrxa',espHeader.tx,'upshift.A','ESP GPIO17/TX2 → U2 A',3.3,'rx',5,['uart'],
    'Board header labelled GPIO17/TX2. The HCT input is powered from 5 V and accepts a 3.3 V TTL-high; verify the exact device datasheet.',[[1305,pins[espHeader.tx].y],[1305,434],[986,434],[986,392]]);
  wire('uartrxb','upshift.Y',`uno.${uno.UART_RX}`,'U2 Y → UNO RX',5,'rx',5,['uart'],
    'Use A0 SoftwareSerial RX; leave UNO D0/D1 free for USB.',[[710,392],[710,423],[282+17*Number(uno.UART_RX.slice(1)),423]]);
  const half=boards.half, mini=boards.mini;
  const g=(id,i)=>pin('bread',id,`GND rail hole ${i+1}`,half.railCol(i),half.gnd,'ground',0,'middle',0);
  const v=(id,i)=>pin('bread',id,`3V3 rail hole ${i+1}`,half.railCol(i),half.vcc,'supply',3.3,'middle',0);
  const gx=id=>pins[`bread.${id}`].x;
  for(const [id,i] of [['HUB_MENU',1],['HUB_SELECT',2],['HUB_FEED',3]]) {
    pin('mini',id,`Lower strip ${mini.groundStrip+1} · hole ${i+1}`,mini.col(mini.groundStrip),mini.bottom[i],'ground',0);
  }
  g('GMINI',23);
  wire('gmini','bread.GMINI','mini.HUB_FEED','Half-size GND → mini ground strip',0,'ground',1,['buttons','power'],
    'Only a GND link. Use the same five-hole lower strip as the MENU and SELECT returns, not the top bank or a neighboring numbered strip.',
    [[gx('GMINI'),761],[mini.col(mini.groundStrip),752]]);
  for (const [i,key] of ['UP','DOWN','MENU','SELECT'].entries()) {
    const onMini=i>=2, b=onMini?mini:half, col=[7,19,2,11][i];
    const x=b.col(col),y=b.top[2],id=key.toLowerCase(),pinName=`D${uno[key]}`;
    part(id,key,x-23,y+5,58,69,`${key} · active-low INPUT_PULLUP on UNO ${pinName}. Use opposite switch sides; verify continuity when unpressed and pressed.`,'Dry contacts only; no separate 5 V jumper.');
    pin(id,'IN','input leg',x,y,'passive',5,'middle',-13);
    pin(id,'RET','return leg',b.col(col+1),b.bottom[1],'passive',0,'middle',16);
    if(!onMini) g(`G${i}`,i===0?7:18);
    wire(`button${i}`,`uno.${pinName}`,`${id}.IN`,key,5,'button',4,['buttons'],
      'Wire to the input-side column; internal pullup, switch closes to ground.',[[ds[uno[key]],107+6*i],[404+14*i,107+6*i],[404+14*i,525+8*i],[x,525+8*i]]);
    const dest=onMini?`mini.HUB_${key}`:`bread.G${i}`;
    wire(`buttonG${i}`,`${id}.RET`,dest,`${key} return`,0,'ground',4,['buttons','power'],
      onMini?'Other switch side → shaded lower GND strip.':'Other switch side → half-size board GND rail.',
      onMini?[[b.col(col+1),mini.bottom[i-1]]]:[[b.col(col+1),752],[gx(`G${i}`),752]]);
  }
  g('GUNO',0); g('GU1',12); g('GOE1',13); g('GA8',15); g('GU2',19); g('GTFT',20); g('GESP',24);
  v('PU1',13); v('PU2',17); v('PDIR',18); v('PTFT',20); v('PESP',24);
  const pg=['power'], gp='Shared ground; make with power off.';
  wire('guno','uno.GND','bread.GUNO','UNO ground',0,'ground',1,pg,gp,[[222,476],[104,476],[104,769]]);
  wire('gu1','lvc.GND','bread.GU1','U1 ground',0,'ground',1,['power','tft','uart'],gp,[[608,452],[482,452],[482,541],[gx('GU1'),541]]);
  wire('lvc-enable','lvc.OE','bread.GOE1','U1 /OE enable',0,'ground',1,['power','tft','uart'],'Active-low; tie /OE pin 19 to GND.',[[690,461],[497,461],[497,547],[gx('GOE1'),547]]);
  wire('ua8','lvc.UNUSED','bread.GA8','Unused U1 A8 input',0,'ground',1,['power','tft'],'Tie A8 pin 9 to ground; corresponding B8 pin 11 is unused and remains open.',[[478,319],[478,435],[524,435],[524,559],[gx('GA8'),559]]);
  wire('gu2','upshift.GND','bread.GU2','U2 ground',0,'ground',1,['power','uart'],gp,[[777,506],[gx('GU2'),506]]);
  wire('u2-enable','upshift.OE','bread.GU2','U2 /OE enable',0,'ground',1,['power','uart'],'Enable the used HCT125 gate by tying its active-low /OE pin 1 to GND.',[[986,444],[986,506],[gx('GU2'),506]]);
  wire('gtft','tft.GND','bread.GTFT','TFT ground',0,'ground',1,['power','tft'],gp,[[751,316],[751,348],[719,348],[719,516],[gx('GTFT'),516]]);
  wire('gesp',espHeader.gnd,'bread.GESP','ESP dev-board GND',0,'ground',1,['power','uart'],gp,[[1038,pins[espHeader.gnd].y],[1038,570],[901,570],[901,775],[gx('GESP'),775]]);
  wire('p3dev',espHeader.v3,'bread.PESP','ESP dev-board 3V3 OUT → logic rail',3.3,'p3',2,pg,
    'The board is powered separately through micro-USB. Its onboard regulator drives the ESP32; use the header 3V3 output for translator/peripheral loads only after confirming current headroom. Never connect a second 3V3 source to this rail.',
    [[1038,pins[espHeader.v3].y],[1038,562],[896,562],[896,787]]);
  wire('p3u1','bread.PU1','lvc.VCC','U1 VCC',3.3,'p3',2,['power','tft','uart'],'100 nF from VCC to GND at U1.',[[gx('PU1'),579],[534,579],[534,484],[546,484]]);
  wire('lvc-dir','bread.PDIR','lvc.DIR','U1 DIR = A → B',3.3,'p3',2,['power','tft','uart'],'Tie DIR pin 1 high to 3V3 to pass UNO signals from A inputs to B outputs.',[[gx('PDIR'),528],[648,528],[648,345]]);
  wire('p5u2','uno.5V','upshift.VCC','U2 VCC · 5 V',5,'p5',2,['power','uart'],'Power only the HCT125 from UNO 5 V. Its 3.3 V input is not connected to the ESP32 5 V rail. Fit 100 nF from VCC to GND.',[[178,415],[699,415],[699,444]]);
  wire('p3tft','bread.PTFT','tft.VCC','TFT VCC (conditional)',3.3,'p3',2,['power','tft'],'Do not connect until breakout VCC range has been verified.',[[gx('PTFT'),538],[728,538],[728,290]],'tft');
  return {parts,pins,wires,colors,boards};
})();

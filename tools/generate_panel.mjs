// Extract the AVR artwork directly from the preserved browser reference.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const read = p => fs.readFileSync(path.join(root,p),'utf8');
const check = process.argv.includes('--check');
function write(p,s) {
  if (check) {
    if (read(p) !== s) throw Error(`${p} is stale; run node tools/generate_panel.mjs`);
  } else fs.writeFileSync(path.join(root,p),s);
}
const cfg = JSON.parse(read('hardware/config.json'));
for (const [family, pins] of Object.entries({Uno:cfg.uno,Esp:cfg.esp,EspOnly:cfg.solo})) {
  if (new Set(Object.values(pins)).size !== Object.keys(pins).length) throw Error(`Duplicate ${family} pin`);
}
const av = s => s.replace(/(^|_)([a-z])/gi,(_,sep,c) => c.toUpperCase());
write('firmware/lib/sntch_panel/src/PanelPins.h',
`// Generated from hardware/config.json by tools/generate_panel.mjs.
#pragma once
#include <stdint.h>
namespace panel {
constexpr uint32_t Baud=${cfg.baud}, DebounceMs=${cfg.debounceMs}, LongMs=${cfg.longMs}, RestartMs=${cfg.restartMs}, PowerMs=${cfg.powerMs};
${Object.entries(cfg.uno).map(([k,v])=>`constexpr uint8_t Uno${av(k.toLowerCase())}=${typeof v==='string' ? 14+Number(v.slice(1)) : v};`).join('\n')}
${Object.entries(cfg.esp).map(([k,v])=>`constexpr uint8_t Esp${av(k.toLowerCase())}=${v};`).join('\n')}
${Object.entries(cfg.solo).map(([k,v])=>`constexpr uint8_t Solo${av(k.toLowerCase())}=${v};`).join('\n')}
constexpr uint8_t Width=${cfg.display.width}, Height=${cfg.display.height}, Rotation=${cfg.display.rotation};
}
#define SNTCH_PANEL_TFT_INIT ${cfg.display.init}
#ifndef SNTCH_PANEL_BACKLIGHT
#define SNTCH_PANEL_BACKLIGHT ${Number(cfg.display.backlightDefault)}
#endif
`);
write('hardware/config.js',`// Generated from hardware/config.json by tools/generate_panel.mjs.\nconst SNTCH_CONFIG = ${JSON.stringify(cfg,null,2)};\n`);

const original = read('hardware/SNTCH_Hardware_Faces_UI_Preview.html');
write('hardware/panel-preview.html',original
  .replace('<title>SNTCH Hardware Face Preview</title>','<title>SNTCH Dual Panel · Browser Simulation</title>')
  .replace('<button class="sw" data-k="power" aria-label="Power"><i></i></button>','<span aria-label="Simulation only" style="color:#a7ddc3;font-size:10px">SIM</span>')
  .replace('<span>ESP32-WROOM-32D</span>','<span>UNO R3 + ESP-32U</span>')
  .replace('data-k="back" aria-label="Back"></button><span>Back','data-k="menu" aria-label="Menu"></button><span>Menu')
  .replaceAll('BACK ', 'MENU ')
  .replace('</body></html>',`<script src="config.js"></script><script src="panel-preview-driver.js"></script></body></html>\n`));
const script = original.split('<script>')[1].split('function zoom(')[0];
const fake = {getContext:()=>({fillRect(){}})};
const ctx = vm.createContext({document:{getElementById:()=>fake,createElement:()=>({...fake})}});
vm.runInContext(script+`
globalThis.assets={SPR,SPC,FC,BGC,ST,SEQ,FONT,ICON,UI,MENU,XT,TR,emitFace,CCOL,
 setTime:(v,start)=>{T=v;tStart=start}};
`,ctx);
const {SPR,SPC,BGC,ST,SEQ,FONT,ICON,UI,MENU,XT,TR,emitFace,CCOL,setTime}=ctx.assets;
if (SPR.length!==52 || SPR.some(r=>r.length!==54) || SEQ.length!==12 || MENU.length!==9) throw Error('Reference dimensions changed');
const spriteKeys = Object.keys(SPC);
const hex565 = c => {
  const n=parseInt(c.slice(1),16);
  return ((n>>19)<<11)|(((n>>10)&63)<<5)|((n>>3)&31);
};
const sprite = SPR.flatMap(row => Array.from({length:27},(_,i)=>
  ((spriteKeys.indexOf(row[i*2])+1)<<4)|(spriteKeys.indexOf(row[i*2+1])+1)));
const mascotFace=emitFace('ready',{g:0,closed:false,eyesOnly:false});
write('hardware/mascot.svg',`<svg xmlns="http://www.w3.org/2000/svg" viewBox="13 4 54 52" shape-rendering="crispEdges" role="img"><title>SNTCH mascot from the original TFT reference</title>\n`+
  SPR.map((r,y)=>[...r.matchAll(/(.)\1*/g)].map(m=>SPC[m[1]]?`<rect x="${m.index+13}" y="${y+4}" width="${m[0].length}" height="1" fill="${SPC[m[1]]}"/>`:'').join('')).join('\n')+
  ['eyeL','eyeR','mouth'].map(k=>{const f=mascotFace[k];
    return f.x.map((x,i)=>`<rect x="${x}" y="${f.y[i]}" width="1" height="1" fill="${CCOL[f.c[i]]}"/>`).join('');
  }).join('')+'\n</svg>\n');
const art = [], frame = [];
const states = SEQ.map((id,i)=>{
  const start=frame.length;
  const variants={
    standby:[-1,0,1],search:[2,3,4],ready:[-1,0,1],active:[-1,0,1],teacher:[-1,0,1],recovery:[-1,0,1]
  }[id] || [0];
  const blinking = ['standby','search','ready','active','attention','teacher','lost','recovery'].includes(id);
  // Live effects are generated independently so they don't multiply gaze/blink data.
  const effect = {search:[3,.4,['wifi']],recovery:[10,1/9,['spin']],completed:[15,.1,['party']]}[id];
  function addFrame(t,g,b,mode) {
    setTime(t,0);
    const f=emitFace(id,{g,closed:b,eyesOnly:false}), m=new Map();
    for(const [k,{x,y,c}] of Object.entries(f)){
      const isFx = effect?.[2].includes(k);
      if ((mode==='base' && isFx) || (mode==='fx' && !isFx)) continue;
      for(let j=0;j<x.length;j++) m.set(y[j]*80+x[j],c[j]);
    }
    frame.push(art.length);
    const runs=[];
    for (const [key,c] of [...m].sort((a,b)=>a[0]-b[0])) {
      const y=Math.floor(key/80), x=key%80, r=runs.at(-1);
      if(r && r[0]===y && r[1]+r[2]===x && r[3]===c && r[2]<16) r[2]++;
      else runs.push([y,x,1,c]);
    }
    for(const [y,x,n,c] of runs) art.push(y,x,((n-1)<<4)|c);
  }
  for(const b of blinking?[false,true]:[false]) for (const g of variants) addFrame(0,g,b,'base');
  const fx=frame.length;
  if(effect) for(let j=0;j<effect[0];j++) addFrame(j*effect[1]+.0001,variants[0],false,'fx');
  return [start,variants.length,Number(blinking),ST[id].cheek==='slash'?1:0,fx,effect?.[0]??0,
    Math.round((effect?.[1]??0)*1000)];
});
if(CCOL.length>16 || frame.length>255 || art.length>65535) throw Error('AVR artwork limits exceeded');
frame.push(art.length);
const font = Array.from({length:59},(_,i)=>{
  const c=String.fromCharCode(32+i),g=FONT[c];
  return g?[c==='.'||c===':'?3:g[0].length+1,...g.map(r=>[...r].reduce((a,b,i)=>a|((b==='#')<<i),0))]:[4,0,0,0,0,0,0,0];
});
const array = (typ,n,data,per=24) => `static const ${typ} ${n}[] PROGMEM = {\n${Array.from({length:Math.ceil(data.length/per)},(_,i)=>'  '+data.slice(i*per,(i+1)*per).join(',')+',').join('\n')}\n};`;
const labels = (name,str) => str.map((s,i)=>`static const char ${name}${i}[] PROGMEM = ${JSON.stringify(s)};`).join('\n')+
`\nstatic const char* const ${name}[] PROGMEM = {${str.map((s,i)=>name+i).join(',')}};`;
write('firmware/lib/sntch_panel/src/PanelArt.h',`// Generated by tools/generate_panel.mjs from the original reference. Do not hand edit.
#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif
namespace art {
constexpr uint16_t FaceBg=${hex565(BGC)}, MorphMs=${Math.round(TR*1000)}, SlideMs=${Math.round(XT*1000)};
${Object.entries(UI).map(([k,v])=>`constexpr uint16_t ${k}=${hex565(v)};`).join('\n')}
${array('uint8_t','sprite',sprite)}
${array('uint16_t','spriteColors',[BGC,...spriteKeys.map(k=>SPC[k])].map(hex565))}
${array('uint16_t','faceColors',CCOL.map(c=>c?hex565(c):0))}
${array('uint8_t','runs',art)}
${array('uint16_t','offset',frame)}
struct FaceArt {uint8_t first,gazes,blink,slash,fx,count; uint16_t period;};
static const FaceArt faces[] PROGMEM = {
${states.map(r=>'  {'+r.join(',')+'},').join('\n')}
};
${array('uint8_t','font',font.flat(),24)}
${labels('caption',SEQ.map(k=>ST[k].label||''))}
${array('uint16_t','captionColor',SEQ.map(k=>ST[k].lc?hex565(ST[k].lc):0))}
${labels('menu',MENU.map(m=>m[0]))}
${array('uint16_t','menuTone',MENU.map(m=>hex565(UI[m[1]])))}
${Object.entries(ICON).map(([k,v])=>array('uint8_t',k,v.map(r=>[...r].reduce((a,b,i)=>a|((b==='#')<<i),0)))).join('\n')}
}
`);

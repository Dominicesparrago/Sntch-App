/* Shared physical reference: column numbers are illustrative, not a board model. */
const SNTCH_BREADBOARDS = (() => {
  const board=(kind,x,y,w,first,pitch)=>{
    const mini=kind==='mini', columns=mini?17:30;
    return {kind,x,y,w,h:mini?211:252,columns,col:i=>first+i*pitch,
      railHoles:mini?0:25,railCol:i=>first+(i+Math.floor(i/5))*pitch,
      top:[46,58,70,82,94].map(d=>y+d), bottom:[141,153,165,177,189].map(d=>y+d),
      gnd:mini?null:y+219, vcc:mini?null:y+237, groundStrip:7};
  };
  function draw(g,b,buttonColumns=[],markedColumns=buttonColumns) {
    const {x,y,w,h,col,columns,top,bottom,gnd,vcc,railHoles,railCol}=b, mini=b.kind==='mini';
    const svg=(tag,attrs,label)=>{
      const el=document.createElementNS('http://www.w3.org/2000/svg',tag);
      for(const [k,v] of Object.entries(attrs)) el.setAttribute(k,v);
      if(label!==undefined) el.textContent=label;
      g.append(el);
    };
    const rect=(x,y,w,h,fill,rx=3,stroke='none',cls='')=>
      svg('rect',{x,y,width:w,height:h,fill,rx,stroke,class:cls});
    const text=(x,y,label,size=10,color='#243e33',anchor='start')=>
      svg('text',{x,y,'font-size':size,fill:color,'text-anchor':anchor,'font-family':'ui-monospace,monospace'},label);
    const bar=(a,c,y,color,type)=>svg('path',{d:`M${a} ${y}H${c}`,fill:'none',stroke:color,'stroke-width':1.5,'data-rail':type});
    rect(x,y,w,h,'#cad9cd',9,'#edf3e6','outline');
    rect(x+10,y+108,w-20,20,'#9caea0',3);
    text(x+13,y+21,mini?'MINI BREADBOARD · NO RAILS':'HALF-SIZE BREADBOARD · POWER RAILS',mini?12:13);
    for(const i of buttonColumns) {
      rect(col(i)-4,top[0]-5,8,top[4]-top[0]+10,'#abc995',2);
      rect(col(i)-4,bottom[0]-5,8,bottom[4]-bottom[0]+10,'#b5c4c8',2);
    }
    for(const [i,row] of [...top,...bottom].entries())
      text(x+10,row+2,'ABCDEFGHIJ'[i],7,'#3b6253');
    if(mini) {
      const gx=col(b.groundStrip);
      rect(gx-5,bottom[0]-6,10,bottom[4]-bottom[0]+12,'#99b5b3',3,'#376972');
      text(x+w/2,y+h-6,'SHADED LOWER STRIP = COMMON GND',9,'#234952','middle');
    } else {
      for(const [rail,color] of [[gnd,'#30576d'],[vcc,'#b8634b']]) {
        bar(railCol(0),railCol(railHoles-1),rail-7,color,'continuous');
        bar(railCol(0),railCol(11),rail-7,color,'split');
        bar(railCol(12),railCol(railHoles-1),rail-7,color,'split');
        for(let i=0;i<railHoles;++i) rect(railCol(i)-2,rail-2,4,4,'#3b5148',1);
      }
      text(x+7,gnd+3,'− GND',8,'#294954'); text(x+7,vcc+3,'+ 3V3',8,'#84351e');
    }
    for(let i=0;i<columns;++i) {
      const a=col(i);
      for(const py of [...top,...bottom]) rect(a-2,py-2,4,4,'#3b5148',1);
      if(i%5===0 || i===columns-1 || markedColumns.includes(i) || (mini&&i===b.groundStrip))
        text(a,top[0]-7,String(i+1),7,'#213c31','middle');
    }
  }
  return {
    dual:{
      half:board('half',107,550,469,138,14),
      mini:board('mini',591,550,296,612,16)
    },
    solo:{
      half:board('half',375,449,626,410,19)
    },
    draw
  };
})();

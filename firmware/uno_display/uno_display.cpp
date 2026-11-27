#include <Arduino.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <PanelButtons.h>
#include <PanelUi.h>

using namespace panel;
using namespace panel_ui;
namespace {
static_assert(UnoTftSck==13 && UnoTftMosi==11,"Hardware SPI pins are fixed on the UNO R3");
constexpr uint32_t RetryMs=1000, StatusMs=1000, QrPollMs=2500, BootMs=3200;
constexpr uint8_t Tries=3;
const uint8_t keys[]={UnoUp,UnoDown,UnoMenu,UnoSelect};
Adafruit_ST7735 tft(UnoTftCs,UnoTftDc,UnoTftReset);
SoftwareSerial uart(UnoUartRx,UnoUartTx);
Buttons buttons;
Reader reader;
Data data;
Ui ui(tft,data);
Frame tx,rx;
uint32_t bootId, cookie=0, now=0, sinceSend=0, lastHello=0, lastStatus=0, lastQr=0;
uint32_t qrRevision=0, offAt=0, beginAt=0, recoveredAt=0, eventUntil=0;
uint16_t qrLength=0, qrCrc=0, qrOffset=0, observedSubmissions=0;
uint8_t qrSidePending=0, attempts=0, todo=0, events[8], head=0, tail=0, observedFlags=0;
bool online=false, sleeping=false, syncWake=false, haveSnapshot=false, faceOverride=false, submitPulse=false;

void changeFace(uint8_t value) {
  Scene s=ui.state();
  if (s.face==value) return;
  data.faceSince=now; s.face=value; ui.set(s,now);
}
void navigate(uint8_t view,uint8_t id,bool in=true) {
  Scene s=ui.state(); s.view=view; s.index=id; ui.set(s,now,in);
}
void backlight(bool on) {
#if SNTCH_PANEL_BACKLIGHT
  digitalWrite(UnoBacklight,on?HIGH:LOW);
#else
  (void)on;
#endif
}
void setFault(uint8_t reason) { data.fault=reason; ui.refresh(); }
void cancelQr() { qrSidePending=0; qrRevision=0; data.side=0; }
void offline(uint8_t request,uint8_t why) {
  online=false; attempts=0; haveSnapshot=false;
  data.flags=0; data.linked=false; data.ssid[0]=data.version[0]=0;
  data.pending=false; lastHello=now-1000;
  if (request==RestartQr || request==Standby || request==Reboot) setFault(why);
  else if (ui.state().view==QrView) setFault(why);
  // Do not replay a state-changing command after a reset/ambiguous timeout.
  todo=0; head=tail=0; cancelQr();
}
void transmit() { sinceSend=now; send(uart,tx); }
void start(uint8_t type,const uint8_t* p=nullptr,uint8_t len=0) {
  tx.type=type; ++tx.seq; tx.size=4+len;
  put32(tx.data,type==Hello?bootId:cookie);
  if(len) memcpy(tx.data+4,p,len);
  attempts=1; transmit();
}
void qrError(uint8_t reason) {
  cancelQr(); data.pending=false; setFault(reason);
}
void receive() {
  while(uart.available()) {
    if (!reader.feed(uart.read(),millis(),rx) || !attempts || rx.seq!=tx.seq || rx.size<4) continue;
    const uint8_t type=tx.type, len=rx.size-4;
    const uint8_t* const p=rx.data+4;
    if (type==Hello) {
      if (rx.type!=HelloAck || len!=4 || get32(p)!=bootId) continue;
      const bool newPeer=cookie!=get32(rx.data);
      cookie=get32(rx.data); online=true; attempts=0;
      lastStatus=now-StatusMs;
      if (newPeer && beginAt+BootMs<=now) { recoveredAt=now; faceOverride=false; }
      if (syncWake) {todo=Wake; syncWake=false;}
      continue;
    }
    if (rx.type==Error && len==1 && p[0]==Reset) { offline(type,Reset); continue; }
    if(get32(rx.data)!=cookie) continue;
    if(rx.type==Error && len==1) {
      attempts=0;
      if(type==GetQr || type==RestartQr || type==ReadQr) {
        if(ui.state().view==QrView) qrError(p[0]); else cancelQr();
      } else if(type!=Button) { data.pending=false; setFault(p[0]); }
      continue;
    }
    if(type==Status && rx.type==Snapshot && len==28) {
      const bool previousLink=data.linked;
      attempts=0; data.linked=true;
      const uint16_t submitted=get16(p+5);
      if (haveSnapshot && submitted>observedSubmissions) { submitPulse=true; eventUntil=now+2700; faceOverride=false; }
      const bool changed=observedFlags!=p[0];
      if(changed) {
        observedFlags=p[0]; faceOverride=false;
        if(previousLink && !(data.flags&Wifi) && (p[0]&Wifi)) recoveredAt=now;
      }
      if (!(p[0]&Wifi) || !(p[0]&Service)) {
        cancelQr();
        if(ui.state().view==QrView && !data.pending && !data.fault) setFault(Offline);
      }
      data.flags=p[0]; data.clients=p[1]; data.sessions=p[2]; data.joined=p[3]; data.used=p[4];
      data.clientsLost=p[7]!=0;
      data.submitted=observedSubmissions=submitted;
      memcpy(data.version,p+8,7); memcpy(data.ssid,p+16,12);
      haveSnapshot=true; ui.refresh();
      continue;
    }
    if ((type==GetQr || type==RestartQr) && rx.type==QrInfo && len==22) {
      attempts=0;
      if(ui.state().view!=QrView || p[0]!=ui.state().index || p[0]!=tx.data[4]) continue;
      const uint8_t n=p[5];
      const uint32_t rev=get32(p+1);
      if(!qrSide(n) || qrScale(n)<2 || get16(p+6)!=qrBytes(n) || !rev) {qrError(Invalid); continue;}
      if(data.side && qrRevision==rev && data.kind==p[0]) {data.pending=false; continue;}
      data.side=0; data.kind=p[0]; data.pending=true; setFault(0);
      qrRevision=rev; qrLength=get16(p+6); qrCrc=get16(p+8); qrOffset=0; qrSidePending=n;
      memcpy(data.code,p+14,8); data.code[8]=0;
      continue;
    }
    if (type==ReadQr && rx.type==QrBits && len>=6 && len<=6+Chunk) {
      attempts=0;
      if(!qrSidePending || ui.state().view!=QrView || get32(p)!=qrRevision) continue;
      const uint8_t count=min(uint16_t(Chunk),uint16_t(qrLength-qrOffset));
      if(get16(p+4)!=qrOffset || len!=6+count) {qrError(Invalid); continue;}
      memcpy(data.matrix+qrOffset,p+6,count); qrOffset+=count;
      if(qrOffset==qrLength) {
        if(crc(data.matrix,qrLength)!=qrCrc) {qrError(Invalid); continue;}
        data.side=qrSidePending; qrSidePending=0; data.qrSince=now;
        data.pending=false; lastQr=now; ui.refresh();
      }
      continue;
    }
    if(rx.type!=Ack || len!=1 || p[0]!=type) continue;
    attempts=0;
    if(type==Button) continue;
    data.pending=false; setFault(0);
    if(type==Standby) {
      cancelQr(); navigate(OffView,0); offAt=now; sleeping=true;
    } else if(type==Reboot) {
      offline(0,0); data.fault=0; lastHello=now+500; beginAt=now;
      // The intentional reboot starts a new boot sequence on the TFT.
      changeFace(Boot); data.faceSince=now; navigate(FaceView,0,false);
    } else if(type==Wake) { recoveredAt=now; faceOverride=false; }
  }
}

void link() {
  receive();
  if (attempts) {
    if(now-sinceSend>=RetryMs) {
      if(attempts<Tries) { ++attempts; transmit(); }
      else offline(tx.type,Timeout);
    }
    return;
  }
  if(!online) {
    if(int32_t(now-lastHello)>=0 && now-lastHello>=700) {lastHello=now; start(Hello);}
    return;
  }
  if(todo) {
    const uint8_t op=todo; todo=0;
    if(op==GetQr || op==RestartQr) {
      uint8_t p[5]={ui.state().index};
      put32(p+1,qrRevision);
      if(op==RestartQr) {data.side=0; qrSidePending=0; ui.refresh();}
      lastQr=now; start(op,p,op==RestartQr && p[0]?5:1);
    } else start(op);
    return;
  }
  if(qrSidePending) {
    uint8_t p[6]; put32(p,qrRevision); put16(p+4,qrOffset); start(ReadQr,p,6); return;
  }
  if(now-lastStatus>=(sleeping?2500:StatusMs)) {lastStatus=now; start(Status); return;}
  if(ui.state().view==QrView && !sleeping && (data.flags&Service) && now-lastQr>=QrPollMs) {
    const uint8_t k=ui.state().index; lastQr=now; start(GetQr,&k,1); return;
  }
  if(head!=tail) {const uint8_t v=events[tail]; tail=(tail+1)%sizeof events; start(Button,&v,1);}
}

bool busy() {
  return todo==Standby || todo==Reboot || (attempts && (tx.type==Standby || tx.type==Reboot || tx.type==RestartQr));
}
void request(uint8_t type) {
  if(!online) { setFault(Timeout); return; }
  todo=type; data.pending=true; ui.refresh();
}
void qrOpen(uint8_t kind) {
  cancelQr(); setFault(0); navigate(QrView,kind); request(GetQr);
}
void input(Buttons::Event event) {
  if(event==Buttons::PowerHold) {
    if(sleeping) {
      sleeping=false; ui.sleep(false,now); backlight(true); setFault(0);
      changeFace(Recovery); recoveredAt=now; faceOverride=false; navigate(FaceView,0,false);
      if (online) request(Wake); else syncWake=true;
    } else if(!busy()) {setFault(0); request(Standby);}
    return;
  }
  if(sleeping || busy()) return;
  const Scene s=ui.state();
  if(event==Buttons::RestartHold) {
    if(s.view==QrView && s.index==1 && (!data.side || data.pending)) return;
    if(s.view!=QrView) {cancelQr(); navigate(QrView,0);}
    setFault(0); request(RestartQr); return;
  }
  if(online && (head+1)%sizeof events!=tail) {events[head]=event; head=(head+1)%sizeof events;}
  const bool held=event>=Buttons::LongUp;
  const uint8_t key=held?event-4:event;
  if(key==Buttons::Menu) {
    setFault(0);
    if(s.view==FaceView) navigate(MenuView,0);
    else if(s.view==MenuView) {faceOverride=false; navigate(FaceView,0,false);}
    else {
      if(s.view==QrView) {cancelQr(); if(todo==GetQr) todo=0; data.pending=false;}
      navigate(MenuView,s.view==QrView?s.index+1:s.index,false);
    }
  } else if(key==Buttons::Up || key==Buttons::Down) {
    if(s.view==FaceView) {faceOverride=true; changeFace((s.face+Faces+(key==Buttons::Up?-1:1))%Faces);}
    else if(s.view==MenuView) navigate(MenuView,(s.index+9+(key==Buttons::Up?-1:1))%9);
  } else if(key==Buttons::Select && !held) {
    if(s.view==FaceView) navigate(MenuView,0);
    else if(s.view==MenuView) {
      if(s.index==1 || s.index==2) qrOpen(s.index-1);
      else {setFault(0); navigate(DetailView,s.index);}
    } else if(s.view==QrView && data.fault) qrOpen(s.index);
    else if(s.view==DetailView && s.index>=7) {setFault(0); request(s.index==7?Reboot:Standby);}
    else if(s.view==DetailView) navigate(MenuView,s.index,false);
  }
}

void statusFace() {
  if(faceOverride || sleeping) return;
  if(now-beginAt<BootMs) {changeFace(Boot); return;}
  if(!online || !data.linked) {changeFace(attempts || cookie?Lost:Recovery); return;}
  if(recoveredAt && now-recoveredAt<1150) {changeFace(Recovery); return;}
  if(!(data.flags&Wifi)) {changeFace(Lost); return;}
  if(!(data.flags&Service)) {changeFace(Recovery); return;}
  if(data.clientsLost) {changeFace(Lost); return;}
  if(submitPulse && int32_t(eventUntil-now)>0) {changeFace(Saved); return;}
  submitPulse=false;
  changeFace(data.flags&Live?(data.joined?Working:Active):
             data.flags&Pairing?Searching:data.flags&Done?Complete:data.flags&TeacherLogin?Ready:Idle);
}
}

void setup() {
  for (uint8_t pin:keys) pinMode(pin,INPUT_PULLUP);
#if SNTCH_PANEL_BACKLIGHT
  pinMode(UnoBacklight,OUTPUT); backlight(false);
#endif
  // Separate UNO epochs even if the ESP32 remains up. EEPROM 0..3 is reserved.
  EEPROM.get(0,bootId); ++bootId; EEPROM.put(0,bootId);
  tft.initR(SNTCH_PANEL_TFT_INIT); tft.setRotation(Rotation);
  uart.begin(Baud);
  beginAt=millis(); data.faceSince=beginAt; lastHello=beginAt-700;
  backlight(true);
}
void loop() {
  now=millis();
  uint8_t mask=0;
  for(uint8_t i=0;i<4;++i) if(!digitalRead(keys[i])) mask|=1<<i;
  buttons.scan(mask,now,input);
  ui.progress(buttons.holding(),buttons.progress(now));
  link();
  statusFace();
  if(sleeping && now-offAt>=500) {
    if(offAt) {ui.sleep(true,now); backlight(false); offAt=0;}
    delay(2);
  } else ui.tick(now);
}

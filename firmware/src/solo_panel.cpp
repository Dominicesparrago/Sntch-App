#include "solo_panel.h"
#if SNTCH_SOLO_PANEL
#include <SPI.h>
#include <esp_system.h>
#include "uptime.h"

namespace sntch_fw {
using namespace panel;
using namespace panel_ui;
namespace {
constexpr uint8_t keys[]={SoloUp,SoloDown,SoloMenu,SoloSelect};
constexpr uint32_t BootMs=3200, StatusMs=500, QrPollMs=1250;
}

SoloPanel::SoloPanel():tft(&SPI,SoloTftCs,SoloTftDc,SoloTftReset),ui(tft,data) {}

void SoloPanel::begin() {
  for(uint8_t pin:keys) pinMode(pin,INPUT_PULLUP);
  // Write-only TFT; no MISO or second chip-select is assigned.
  digitalWrite(SoloTftCs,HIGH); pinMode(SoloTftCs,OUTPUT);
  SPI.begin(SoloSck,-1,SoloMosi,SoloTftCs);
  tft.initR(SNTCH_PANEL_TFT_INIT);
  tft.setRotation(Rotation);
  tft.setSPISpeed(8000000);  // conservative on short jumpers
  data.linked=true;  // local display: no UART or separate display controller
  strncpy(data.version,SNTCH_FIRMWARE_VERSION,sizeof data.version-1);
  offers.seed(esp_random());
  started=millis();
  data.faceSince=started;
  lastStatus=started-StatusMs;
  for(uint8_t row=0;row<Height;++row) ui.tick(started);
}

void SoloPanel::navigate(uint8_t view,uint8_t index,uint32_t now,bool entering) {
  auto s=ui.state();
  s.view=view; s.index=index;
  ui.set(s,now,entering);
}
void SoloPanel::face(uint8_t value,uint32_t now) {
  auto s=ui.state();
  if(s.face==value) return;
  data.faceSince=now;
  s.face=value;
  ui.set(s,now);
}

void SoloPanel::syncStatus(const sntch::Classroom& room,sntch::Ms time,bool wifi,bool service,
                           bool fs,uint8_t clients,uint8_t used,const char* ssid,uint32_t now) {
  const auto counts=panelCounts(room,time);
  const bool teacher=room.hasTeacherLogin(time);
  seenClient |= clients!=0;
  const uint8_t flags=(wifi?Wifi:0)|(service?Service:0)|(fs&&!room.storageError()?Storage:0)|
    (sleeping?Paused:0)|(counts.live?Live:0)|(counts.done?Done:0)|
    (offers.searching()&&!teacher?Pairing:0)|(teacher?TeacherLogin:0);
  const bool lost=seenClient&&!clients;
  if(flags!=data.flags || lost!=data.clientsLost) {
    if(haveStatus && service && !(data.flags&Service) && bootedService) recoveredAt=now;
    manualFace=false;
  }
  bootedService |= service;
  const auto submitted=room.submissionCount();
  if(haveStatus && submitted>data.submitted) {
    submitPulse=true; eventUntil=now+2700; manualFace=false;
  }
  if(flags!=data.flags || clients!=data.clients || used!=data.used || submitted!=data.submitted ||
     counts.open!=data.sessions || counts.joined!=data.joined) ui.refresh();
  data.flags=flags; data.clients=clients; data.used=used; data.submitted=submitted;
  data.sessions=counts.open; data.joined=counts.joined; data.clientsLost=lost;
  strncpy(data.ssid,ssid,sizeof data.ssid-1);
  haveStatus=true;
}

void SoloPanel::showQr(sntch::Classroom& room,sntch::Ms time,bool ready,const char* ip,
                       uint8_t kind,bool rotate,uint32_t now) {
  lastQr=now;
  const bool explicitAction=rotate || data.kind!=kind || !data.side;
  const auto why=offers.refresh(room,time,ready,ip,kind,rotate,shownRevision,
                                rotate&&!kind?esp_random():0);
  if(why) {
    if(data.side || data.fault!=why) {
      data.side=0; shownRevision=0; data.fault=why; ui.refresh();
    }
    return;
  }
  if(explicitAction || shownRevision!=offers.revision || data.fault) {
    data.kind=kind; data.side=offers.size; data.fault=0; data.qrSince=now;
    shownRevision=offers.revision;
    memcpy(data.code,offers.label,sizeof data.code);
    memcpy(data.matrix,offers.matrix,offers.length);
    ui.refresh();
  }
}

void SoloPanel::power(sntch::Classroom& room,bool fs,uint32_t now) {
  if(sleeping) {
    if(tftAsleep) ui.sleep(false,now);
    sleeping=tftAsleep=false;
    data.fault=0; recoveredAt=now; manualFace=false;
    face(Recovery,now); navigate(FaceView,0,now,false);
    return;
  }
  if(!fs || !room.save()) { lastQr=now; data.fault=Store; ui.refresh(); return; }
  data.side=0; shownRevision=0; data.fault=0;
  sleeping=true; offAt=now;
  navigate(OffView,0,now);
}

void SoloPanel::gesture(Buttons::Event event,sntch::Classroom& room,sntch::Ms time,
                        bool ready,bool fs,const char* ip,uint32_t now) {
  if(rebootAt) return;
  if(event==Buttons::PowerHold) { power(room,fs,now); return; }
  if(sleeping) return;
  const auto s=ui.state();
  if(event==Buttons::RestartHold) {
    const uint8_t kind=s.view==QrView?s.index:0;
    if(kind && !data.side) { showQr(room,time,ready,ip,1,false,now); return; }
    if(s.view!=QrView) { data.side=0; shownRevision=0; navigate(QrView,kind,now); }
    showQr(room,time,ready,ip,kind,true,now);
    return;
  }
  const bool held=event>=Buttons::LongUp;
  const uint8_t key=held?event-4:event;
  if(key==Buttons::Menu) {
    data.fault=0;
    if(s.view==FaceView) navigate(MenuView,0,now);
    else if(s.view==MenuView) {manualFace=false; navigate(FaceView,0,now,false);}
    else {
      if(s.view==QrView) {data.side=0; shownRevision=0;}
      navigate(MenuView,s.view==QrView?s.index+1:s.index,now,false);
    }
  } else if(key==Buttons::Up || key==Buttons::Down) {
    const int step=key==Buttons::Up?-1:1;
    if(s.view==FaceView) { manualFace=true; face((s.face+Faces+step)%Faces,now); }
    else if(s.view==MenuView) navigate(MenuView,(s.index+9+step)%9,now);
  } else if(key==Buttons::Select && !held) {
    if(s.view==FaceView) navigate(MenuView,0,now);
    else if(s.view==MenuView) {
      data.fault=0;
      if(s.index==1 || s.index==2) {
        data.side=0; shownRevision=0;
        navigate(QrView,s.index-1,now);
        showQr(room,time,ready,ip,s.index-1,false,now);
      } else navigate(DetailView,s.index,now);
    } else if(s.view==QrView && data.fault) showQr(room,time,ready,ip,s.index,false,now);
    else if(s.view==DetailView && s.index==8) power(room,fs,now);
    else if(s.view==DetailView && s.index==7) {
      if(!fs || !room.save()) { data.fault=Store; ui.refresh(); return; }
      data.fault=0; data.faceSince=now;
      face(Boot,now); navigate(FaceView,0,now,false);
      rebootAt=now+350;
      if(!rebootAt) rebootAt=1;
    } else if(s.view==DetailView) navigate(MenuView,s.index,now,false);
  }
}

void SoloPanel::poll(sntch::Classroom& room,bool wifi,bool service,bool fs,uint8_t clients,
                     uint8_t used,const char* ip,const char* ssid) {
  const auto time=uptimeMs();
  const uint32_t now=millis();
  if(rebootAt && int32_t(now-rebootAt)>=0) { ESP.restart(); return; }
  if(!wifi) offers.disconnected();
  if(now-lastStatus>=StatusMs) {
    lastStatus=now;
    syncStatus(room,time,wifi,service,fs,clients,used,ssid,now);
  }
  uint8_t mask=0;
  for(uint8_t i=0;i<sizeof keys;++i) if(digitalRead(keys[i])==LOW) mask|=1<<i;
  const bool ready=wifi&&service;
  buttons.scan(mask,now,[&](auto event){ gesture(event,room,time,ready,fs,ip,now); });
  ui.progress(buttons.holding(),buttons.progress(now));
  const auto s=ui.state();
  if(s.view==QrView && !sleeping && (!ready || now-lastQr>=QrPollMs))
    showQr(room,time,ready,ip,s.index,false,now);
  if(!manualFace && !sleeping && !rebootAt) {
    if(now-started<BootMs) face(Boot,now);
    else if(!fs || (recoveredAt && now-recoveredAt<1150)) face(Recovery,now);
    else if(!wifi || data.clientsLost) face(Lost,now);
    else if(!service) face(Recovery,now);
    else if(submitPulse && int32_t(eventUntil-now)>0) face(Saved,now);
    else {
      submitPulse=false;
      face(data.flags&Live?(data.joined?Working:Active):
           data.flags&Pairing?Searching:data.flags&Done?Complete:data.flags&TeacherLogin?Ready:Idle,now);
    }
  }
  if(sleeping && now-offAt>=500) {
    if(!tftAsleep) {ui.sleep(true,now); tftAsleep=true;}
    return; // AP, HTTP, exam clocks and storage still run.
  }
  // Bound each SPI slice so the main loop can return to HTTP/UI input work.
  const uint32_t frameStart=micros();
  for(uint8_t i=0;i<8 && micros()-frameStart<1800;++i) ui.tick(now);
}
}
#endif

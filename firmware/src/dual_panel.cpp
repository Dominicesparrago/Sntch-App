#include "dual_panel.h"
#if SNTCH_DUAL_PANEL
#include <esp_system.h>
#include <PanelPins.h>
#include <qrcodegen.h>
#include "sntch_config.h"
#include "uptime.h"

using namespace panel;
namespace sntch_fw {
void DualPanel::begin() {
  boot = esp_random();
  offers.seed(esp_random());
  Serial2.begin(Baud,SERIAL_8N1,EspUartRx,EspUartTx);
}
void DualPanel::response(Type type,uint8_t len) {
  reply.type=type; reply.seq=in.seq; reply.size=4+len;
  put32(reply.data,boot);
}
void DualPanel::error(Fault why) { response(Error,1); reply.data[4]=why; }

void DualPanel::poll(sntch::Classroom& c, bool wifi, bool service, bool fs,
                     uint8_t clients,uint8_t usedPct,const char* ip,const char* ssid) {
  const auto now=uptimeMs();
  if (rebootAt && int32_t(millis()-rebootAt)>=0) ESP.restart();
  if (clients) seenClient=true;
  if (!wifi) offers.disconnected();
  while (Serial2.available()) {
    if (!reader.feed(Serial2.read(),millis(),in)) continue;
    if (in.type==Hello && in.size==4) {
      const uint32_t id=get32(in.data);
      if (!havePeer || peer!=id) {
        peer=id; boot=esp_random(); havePeer=true; havePrevious=false; standby=false;
      }
      response(HelloAck,4); put32(reply.data+4,id);
      send(Serial2,reply);
      continue;
    }
    if (in.size<4) continue;
    if (!havePeer || get32(in.data)!=boot) {
      // No current server/peer epoch: don't execute stale commands.
      error(Reset); send(Serial2,reply);
      continue;
    }
    if (havePrevious) {
      if (previous.seq==in.seq) {
        if (previous.type==in.type && previous.size==in.size && !memcmp(previous.data,in.data,in.size))
          send(Serial2,cached);
        continue;
      }
      if (!newer(in.seq,previous.seq)) continue;
    }
    uint8_t* const o=reply.data+4;
    const uint8_t* const p=in.data+4;
    const uint8_t len=in.size-4;
    error(Invalid);
    switch (in.type) {
      case Status: if (!len) {
        const auto counts=panelCounts(c,now);
        response(Snapshot,28);
        o[0]=(wifi?Wifi:0)|(service?Service:0)|(fs&&!c.storageError()?Storage:0)|
             (standby?Paused:0)|(offers.searching()&&!c.hasTeacherLogin(now)?Pairing:0)|
             (counts.done?Done:0)|(counts.live?Live:0)|(c.hasTeacherLogin(now)?TeacherLogin:0);
        o[1]=clients; o[2]=counts.open; o[3]=counts.joined;
        o[4]=usedPct; put16(o+5,c.submissionCount());
        o[7] = seenClient && !clients ? 1 : 0;
        memset(o+8,0,20);
        strncpy(reinterpret_cast<char*>(o+8),SNTCH_FIRMWARE_VERSION,7);
        strncpy(reinterpret_cast<char*>(o+16),ssid,12);
        break;
      } break;
      case Button:
        if (len==1 && p[0]>=1 && p[0]<=8) {
          // Physical input events are informational; classroom operations have
          // their own explicitly validated commands.
          response(Ack,1); o[0]=Button;
        }
        break;
      case Standby: case Wake: case Reboot:
        if (len) break;
        if (in.type!=Wake && (!fs || !c.save())) { error(Store); break; }
        standby=in.type==Standby;
        if (in.type==Reboot) rebootAt=millis()+300;
        response(Ack,1); o[0]=in.type;
        break;
      case GetQr: case RestartQr: {
        if (!len || p[0]>1 || standby || len!=(in.type==RestartQr && p[0]?5:1)) break;
        const bool rotate=in.type==RestartQr;
        const auto why=offers.refresh(c,now,wifi&&service,ip,p[0],rotate,
                                      rotate&&p[0]?get32(p+1):0,rotate&&!p[0]?esp_random():0);
        if(why) { error(Fault(why)); break; }
        response(QrInfo,22);
        o[0]=offers.kind; put32(o+1,offers.revision); o[5]=offers.size;
        put16(o+6,offers.length); put16(o+8,offers.checksum);
        put32(o+10,offers.session); memcpy(o+14,offers.label,8);
        break;
      }
      case ReadQr: {
        if (len!=6) break;
        const uint16_t offset=get16(p+4);
        if (!offers.size || get32(p)!=offers.revision || offset>=offers.length || offset%Chunk) { error(Stale); break; }
        if (!wifi || !service) { error(Offline); break; }
        if (offers.kind && !offers.currentActivity(c,now)) { offers.size=0; error(Stale); break; }
        const uint8_t count=min(uint16_t(Chunk),uint16_t(offers.length-offset));
        response(QrBits,6+count);
        put32(o,offers.revision); put16(o+4,offset); memcpy(o+6,offers.matrix+offset,count);
        break;
      }
    }
    previous=in; cached=reply; havePrevious=true;
    send(Serial2,reply);
  }
}
}
#endif

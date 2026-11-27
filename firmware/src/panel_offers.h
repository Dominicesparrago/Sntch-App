#pragma once
#include <PanelWire.h>
#include <qrcodegen.h>
#include <cstdio>
#include "sntch/classroom.hpp"
#include "sntch_config.h"

namespace sntch_fw {
// Common classroom authority and QR encoding, independent of the display
// transport. Called from the same loop as the HTTP API (no cross-task access
// to Classroom or its persistent state).
class PanelOffers {
 public:
  uint32_t revision=0;
  unsigned session=0;
  uint16_t length=0, checksum=0;
  uint8_t matrix[panel::MatrixMax], kind=0, size=0;
  char label[9]={};

  void seed(uint32_t code) { pairingCode=code; }
  void disconnected() { size=0; pairing=false; }
  bool searching() const { return pairing; }
  bool currentActivity(sntch::Classroom& room, sntch::Ms now) const {
    unsigned sid=0, joined=0;
    std::string code;
    return size && kind==1 && room.liveActivityQr(now,false,sid,code,joined).ok &&
      sid==session && code==label;
  }
  // Zero means success. Activity rotation requires proof that the panel
  // displayed the currently advertised session generation.
  uint8_t refresh(sntch::Classroom& room, sntch::Ms now, bool ready, const char* ip,
                  uint8_t requested, bool rotate, uint32_t displayed, uint32_t newCode) {
    using namespace panel;
    if(requested>1) return Invalid;
    if(!ready) return Offline;
    unsigned sid=0, joined=0;
    char payload[64], newLabel[9]={};
    if(requested) {
      std::string code;
      auto r=room.liveActivityQr(now,false,sid,code,joined);
      if(r.ok && rotate) {
        if (!size || kind!=1 || session!=sid || code!=label || displayed!=revision) return Stale;
        r=room.liveActivityQr(now,true,sid,code,joined);
      }
      if(!r.ok) return r.code=="MULTIPLE_LIVE"?Ambiguous:r.status==404?NoSession:r.status==500?Store:Invalid;
      std::snprintf(newLabel,sizeof newLabel,"%s",code.c_str());
      std::snprintf(payload,sizeof payload,"%s",newLabel);
    } else {
      if(!ip || !*ip) return Offline;
      if(rotate) pairingCode=pairingCode==newCode?newCode^0x80000000UL:newCode;
      std::snprintf(newLabel,sizeof newLabel,"%08lX",static_cast<unsigned long>(pairingCode));
#if SNTCH_HTTP_PORT == 80
      const int n=std::snprintf(payload,sizeof payload,"sntch://pair?url=http://%s&sid=%s",ip,newLabel);
#else
      const int n=std::snprintf(payload,sizeof payload,"sntch://pair?url=http://%s:%u&sid=%s",ip,
                                static_cast<unsigned>(SNTCH_HTTP_PORT),newLabel);
#endif
      if(n<0 || n>=int(sizeof payload)) return TooLarge;
    }
    if(!size || rotate || kind!=requested || session!=sid || strcmp(label,newLabel) ||
       (!requested && strcmp(lastIp,ip))) {
      if(!encode(payload)) { size=0; return TooLarge; }
      kind=requested; session=sid;
      memcpy(label,newLabel,sizeof label);
      if(!requested) std::snprintf(lastIp,sizeof lastIp,"%s",ip);
    }
    pairing=!requested;
    return 0;
  }
 private:
  uint32_t pairingCode=0;
  char lastIp[16]={};
  bool pairing=false;
  bool encode(const char* payload) {
    using namespace panel;
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(5)], modules[sizeof tmp];
    if(!qrcodegen_encodeText(payload,tmp,modules,qrcodegen_Ecc_MEDIUM,1,5,qrcodegen_Mask_AUTO,true))
      return false;
    size=qrcodegen_getSize(modules);
    if(!qrSide(size) || qrScale(size)<2) return false;
    length=qrBytes(size);
    memset(matrix,0,length);
    for(uint16_t k=0;k<uint16_t(size)*size;++k)
      if(qrcodegen_getModule(modules,k%size,k/size)) matrix[k/8] |= 0x80 >> (k%8);
    checksum=crc(matrix,length);
    if(!++revision) ++revision;
    return true;
  }
};

struct PanelCounts {
  uint8_t open=0, joined=0;
  bool live=false, done=false;
};
inline PanelCounts panelCounts(const sntch::Classroom& room,sntch::Ms now) {
  PanelCounts n;
  unsigned joined=0;
  for(const auto& s:room.sessions()) {
    if(s.state=="ended") n.done=true;
    if(s.expiresAtMs<=now) continue;
    if(s.state=="ready" || s.state=="live" || s.state=="paused") ++n.open;
    if(s.state=="live") { n.live=true; joined+=s.joined.size(); }
  }
  n.joined=joined>255?255:joined;
  return n;
}
}

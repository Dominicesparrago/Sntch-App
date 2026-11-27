#pragma once
#include <Arduino.h>
#include <Adafruit_ST7735.h>
#include <PanelWire.h>
#include <PanelPins.h>
#include "PanelArt.h"

namespace panel_ui {
using namespace panel;
enum View : uint8_t { FaceView, MenuView, DetailView, QrView, OffView };
#if SNTCH_PHONE_ADMIN
constexpr uint8_t MicroSdUnavailable = Timeout + 1;
#endif
struct Scene {
  uint8_t view=FaceView, index=0, face=Boot;
};
struct Data {
  uint8_t flags=0, clients=0, sessions=0, joined=0, used=0;
  uint16_t submitted=0;
  char version[8]={}, ssid[13]={};
  uint8_t matrix[MatrixMax], side=0, kind=0, fault=0;
  char code[9]={};
#if SNTCH_PHONE_ADMIN
  char address[22]={};
#endif
  bool linked=false, pending=false, clientsLost=false;
  uint32_t qrSince=0, faceSince=0;
};

// All primitives target a single RGB565 scanline. SPI flushes it immediately.
// There is no framebuffer and no heap allocation.
class Ui {
  Adafruit_ST7735& tft;
  uint16_t line[Width];
  const Data& d;
  Scene current, previous, drawn, under;
  uint32_t change=0, clock=0, lastDraw=0, faceSince=0, wakeAt=0;
  uint16_t transition=0, hold=0, frameHold=0;
  uint8_t y=Height, holdKind=0, frameKind=0;
  int16_t shift=0;
  bool inward=true, frameInward=true, dirty=true, stopped=false, restartFrame=false, waking=false;

  void rect(int16_t x,int16_t top,int16_t w,int16_t h,uint16_t color) {
    if (y<top || y>=top+h) return;
    x+=shift;
    int16_t end=min(int16_t(Width),int16_t(x+w));
    for (int16_t i=max(int16_t(0),x);i<end;++i) line[i]=color;
  }
  static const uint8_t* glyph(char c) {
    if (c>='a' && c<='z') c-=32;
    if (c<32 || c>90 || c=='@') c='?';
    return art::font + uint16_t(c-32)*8;
  }
  int16_t text(int16_t x,int16_t top,const char* p,uint16_t c,bool flash=true) {
    const int8_t row=int16_t(y)-top;
    // When called off-row return immediately; width is computed separately.
    if (row<0 || row>=7) return x;
    for (char ch; (ch=flash?pgm_read_byte(p):*p); ++p) {
      const uint8_t* g=glyph(ch);
      uint8_t b=pgm_read_byte(g+1+row);
      for (uint8_t j=0;b;++j,b>>=1) if (b&1) rect(x+j,y,1,1,c);
      x+=pgm_read_byte(g);
    }
    return x;
  }
  int16_t width(const char* p) {
    int16_t w=0;
    for (char ch; (ch=pgm_read_byte(p++));) w+=pgm_read_byte(glyph(ch));
    return w?w-1:0;
  }
  void number(int16_t x,int16_t top,uint16_t v) {
    if (y<top || y>=top+7) return;
    char b[6]; utoa(v,b,10); text(x,top,b,art::ink,false);
  }
  void header(const char* p,uint16_t tone) {
    rect(0,0,160,16,art::hdr); rect(6,6,4,4,tone); text(14,5,p,art::ink);
  }
  void footer(const char* left,const char* right) {
    rect(0,116,160,12,0x0083); text(6,120,left,art::mute);
    if (y>=120 && right) text(154-width(right),120,right,art::mute);
  }
  const char* menuTitle(uint8_t i) { return reinterpret_cast<const char*>(pgm_read_ptr(art::menu+i)); }
  uint16_t tone(uint8_t i) { return pgm_read_word(art::menuTone+i); }
  void menu(uint8_t s) {
    header(PSTR("MENU"),art::ok);
    if (y>=19 && y<109) {
      const uint8_t i=(y-19)/10, t=19+i*10;
      if (i==s) { rect(2,t,156,10,art::sel); rect(2,t,2,10,art::acc); }
      rect(8,t+3,4,4,tone(i)); text(16,t+1,menuTitle(i),i==s?art::ink:art::body);
    }
    footer(PSTR("SEL OPEN"),PSTR("MENU EXIT"));
  }
  const char* fault() {
    switch (d.fault) {
      case Offline:return PSTR("WI-FI NOT READY");
      case NoSession:return PSTR("NO LIVE ACTIVITY");
      case Ambiguous:return PSTR("MULTIPLE LIVE ACTIVITIES");
      case TooLarge:return PSTR("CODE TOO LARGE");
      case Store:return PSTR("SAVE FAILED");
      case Stale:return PSTR("CODE NO LONGER VALID");
      case Invalid:return PSTR("REQUEST REJECTED");
      case Timeout:return PSTR("LINK / REQUEST TIMEOUT");
#if SNTCH_PHONE_ADMIN
      case MicroSdUnavailable:return PSTR("MICROSD UNAVAILABLE");
#endif
      default:return PSTR("CONTROLLER RESET");
    }
  }
  void detail(uint8_t i) {
    header(menuTitle(i),tone(i));
    switch (i) {
      case 0:
#if SNTCH_SOLO_PANEL
        text(8,26,d.flags&Service?PSTR("ESP32 SERVICE READY"):PSTR("SERVICE UNAVAILABLE"),art::ink);
#else
        text(8,26,d.linked?PSTR("CONTROLLER ESP32 OK"):PSTR("CONTROLLER LINK LOST"),art::ink);
#endif
        text(8,37,d.flags&Wifi?PSTR("WI-FI AP ON"):PSTR("WI-FI AP OFF"),art::body);
        text(8,48,d.ssid,art::body,false);
        text(8,59,PSTR("CLIENTS"),art::body); if(d.linked) number(55,59,d.clients);
        text(8,70,PSTR("FW"),art::body); text(26,70,d.version,art::ink,false);
        break;
      case 3:
        text(8,26,d.linked?(d.flags&Live?PSTR("STATUS LIVE"):PSTR("NO LIVE SESSION")):PSTR("STATUS UNAVAILABLE"),art::ink);
        text(8,37,PSTR("OPEN SESSIONS"),art::body); if(d.linked) number(100,37,d.sessions);
        text(8,48,PSTR("JOINED LIVE"),art::body); if(d.linked) number(100,48,d.joined);
        text(8,59,PSTR("SUBMISSIONS"),art::body); if(d.linked) number(100,59,d.submitted);
        break;
      case 4:
        text(8,26,PSTR("HELP DATA UNAVAILABLE"),art::ink);
        text(8,37,PSTR("NO CONTROLLER FEED"),art::body);
        break;
      case 5:
        text(8,26,d.flags&Storage?PSTR("LITTLEFS READY"):PSTR("LITTLEFS UNAVAILABLE"),art::ink);
        if(d.flags&Storage) {text(8,37,PSTR("USED PERCENT"),art::body); number(88,37,d.used);}
        text(8,48,PSTR("ESP32 FLASH STORAGE"),art::body);
        break;
      case 6:
#if SNTCH_SOLO_PANEL
        text(8,26,PSTR("ESP32 LOCAL DISPLAY"),art::ink);
#else
        text(8,26,d.linked?PSTR("DUAL UART LINKED"):PSTR("UART NO RESPONSE"),art::ink);
#endif
        text(8,37,PSTR("FOUR BUTTONS CONFIGURED"),art::body);
        text(8,48,PSTR("TFT ST7735 SPI"),art::body);
        break;
      case 7: case 8:
        text(8,26,i==7?PSTR("RESTART CONTROLLER?"):PSTR("SOFTWARE STANDBY?"),art::ink);
        text(8,48,PSTR("SEL CONFIRM"),art::body); text(8,59,PSTR("MENU CANCEL"),art::body);
        break;
    }
    if (d.pending) text(8,91,PSTR("WAITING FOR CONTROLLER"),art::warn);
    if (d.fault) text(8,91,fault(),art::warn);
    footer(i>=7?PSTR("SEL CONFIRM"):PSTR("MENU RETURN"),i>=7?PSTR("MENU CANCEL"):nullptr);
  }
  void qr(uint8_t k) {
    const bool show=d.side && d.kind==k && !d.fault && d.linked;
    const uint16_t elapsed=min(uint32_t(600),clock-d.qrSince);
    const bool building=show && elapsed<600;
    header(k?art::menu2:art::menu1,show&&!building?tone(k+1):art::warn);
    if (!show) {
      text(8,36,d.fault?fault():PSTR("WAITING FOR QR"),art::ink);
      if (d.fault==TooLarge) text(8,51,PSTR("USE ADDRESS / JOIN CODE"),art::body);
#if SNTCH_PHONE_ADMIN
      else if (d.fault==MicroSdUnavailable) text(8,51,PSTR("CHECK CARD AND RESTART"),art::body);
      else if (d.fault==Offline) text(8,51,PSTR("RETRYING AUTOMATICALLY"),art::body);
#else
      else if (d.fault) text(8,51,PSTR("SELECT TO RETRY"),art::body);
#endif
    } else {
      const uint8_t s=qrScale(d.side), box=(d.side+8)*s;
      rect(8,24,box,box,0xffff);
      const int16_t row=int16_t(y)-24-4*s;
      const uint8_t rows=elapsed*d.side/600;
      if (row>=0 && row/s<rows) {
        uint16_t b=uint16_t(row/s)*d.side;
        for (uint8_t i=0;i<d.side;++i,++b)
          if (d.matrix[b/8] & (0x80>>(b%8))) rect(8+(i+4)*s,y,s,1,art::dark);
      }
      if (building) rect(10,24+(rows+4)*s,box-4,1,art::acc);
      text(104,26,k?PSTR("STUDENTS"):PSTR("TEACHERS"),art::acc);
      text(104,38,PSTR("SCAN TO"),art::body);
      text(104,48,k?PSTR("JOIN"):PSTR("PAIR"),art::body);
#if SNTCH_PHONE_ADMIN
      text(104,66,k?PSTR("CODE"):PSTR("WI-FI"),art::mute);
      text(100,76,k?d.code:d.ssid,art::ink,false);
#else
      text(104,66,k?PSTR("CODE"):PSTR("OFFER ID"),art::mute);
      text(104,76,d.code,art::ink,false);
#endif
      text(104,90,k?PSTR("LIVE"):(d.flags&TeacherLogin?PSTR("SIGNED IN"):PSTR("PAIRING")),art::body);
      text(104,104,building?PSTR("BUILDING"):PSTR("READY"),building?art::warn:art::acc);
    }
#if SNTCH_PHONE_ADMIN
    footer(PSTR(""),nullptr);
    if (show || d.fault==TooLarge) text(6,120,d.address,art::mute,false);
#else
    footer(PSTR("SNTCH QR"),PSTR("MENU EXIT"));
#endif
  }
  void overlay(uint8_t frame,int8_t r,int8_t bob) {
    uint16_t begin=pgm_read_word(art::offset+frame),end=pgm_read_word(art::offset+frame+1);
    // Binary search on row to keep animation time independent of asset size.
    uint16_t lo=0,hi=(end-begin)/3;
    while (lo<hi) {
      const uint16_t m=(lo+hi)/2;
      if (pgm_read_byte(art::runs+begin+m*3)<r) lo=m+1; else hi=m;
    }
    for (begin+=lo*3; begin<end && pgm_read_byte(art::runs+begin)==r;begin+=3) {
      const uint8_t x=pgm_read_byte(art::runs+begin+1),v=pgm_read_byte(art::runs+begin+2);
      rect(x*2,r*2+bob,2*(1+(v>>4)),2,pgm_read_word(art::faceColors+(v&15)));
    }
  }
  void face(uint8_t i) {
    const int8_t bob=((clock%3000)>120 && (clock%3000)<1370);
    const int8_t r=(int16_t(y)-bob)/2;
    if (y<bob) return;
    const uint32_t age=clock-faceSince;
    // A cheap row-latched reveal keeps the original animation duration. The
    // browser retains its full signed-distance feature morph.
    const uint8_t old=frameOldFace;
    const bool useOld=age<art::MorphMs && (uint16_t(r)*47%64)*art::MorphMs/64>age;
    art::FaceArt f;
    memcpy_P(&f,art::faces+(useOld?old:i),sizeof f);
    if (r>=4 && r<56) {
      const uint8_t* b=art::sprite+(r-4)*27;
      for (uint8_t j=0;j<27;++j) {
        const uint8_t v=pgm_read_byte(b+j);
        if(v>>4)rect(26+j*4,y,2,1,pgm_read_word(art::spriteColors+(v>>4)));
        if(v&15)rect(28+j*4,y,2,1,pgm_read_word(art::spriteColors+(v&15)));
      }
    }
    if (r>=42 && r<45) {
      const int8_t sl=1-(r-42);
      for (uint8_t j=0;j<3;++j) {
        rect((26+j*3+(f.slash?sl:-sl))*2,y,2,1,pgm_read_word(art::faceColors+1));
        rect((48+j*3+sl)*2,y,2,1,pgm_read_word(art::faceColors+1));
      }
    }
    const uint8_t gaze=f.gazes==1?0:i==Searching?(age/400)%3:(clock/2900)%3;
    const bool blink=f.blink && clock%4400>=4240;
    if (r<48) overlay(f.first+gaze+(blink?f.gazes:0),r,bob);
    if(f.count) overlay(f.fx+((i==Searching?age:clock)/f.period)%f.count,r,bob);
    if (y<98 || y>=113) return;
    const uint8_t caption=age<art::MorphMs/2?old:i;
    const char* p=reinterpret_cast<const char*>(pgm_read_ptr(art::caption+caption));
    if (!pgm_read_byte(p)) return;
    const uint8_t iw=caption==Saved?9:caption==Lost?7:0;
    const uint8_t w=max(int16_t(caption==Boot?92:0),int16_t(width(p)+iw+12));
    const uint8_t l=(160-w)/2;
    rect(l+1,98,w-2,15,0x0883); rect(l,99,w,13,0x0883);
    if(caption==Boot) rect(l+2,100,(w-4)*min(age,uint32_t(3200))/3200,11,0x4acc);
    const uint16_t color=pgm_read_word(art::captionColor+caption);
    if (iw && y>=103 && y<(caption==Saved?109:108)) {
      uint8_t v=pgm_read_byte((caption==Saved?art::check:art::cross)+y-103);
      for (uint8_t j=0;v;++j,v>>=1) if(v&1) rect(l+6+j,y,1,1,color);
    }
    text(l+6+iw,102,p,color);
  }
  void scene(const Scene& s,int16_t x) {
    shift=x;
    rect(0,0,Width,Height,s.view==FaceView?art::FaceBg:s.view==OffView?0:art::bg);
    switch (s.view) {
      case FaceView: face(s.face); break;
      case MenuView: menu(s.index); break;
      case DetailView: detail(s.index); break;
      case QrView: qr(s.index); break;
      case OffView: text(14,60,PSTR("STANDBY - HOLD UP/DOWN"),art::mute); break;
    }
  }
  uint8_t oldFace=Boot, frameOldFace=Boot;
 public:
  Ui(Adafruit_ST7735& t,const Data& data):tft(t),d(data) {}
  Scene state() const { return current; }
  void set(Scene s,uint32_t now,bool entering=true) {
    if (s.view!=current.view) {
      previous=current; change=now; inward=entering; restartFrame=true;
    }
    if(s.face!=current.face) oldFace=current.face;
    current=s; dirty=true;
  }
  void refresh() { dirty=true; }
  void sleep(bool sleep,uint32_t now) {
    stopped=sleep;
    waking=!sleep;
    if(sleep) {tft.sendCommand(ST77XX_DISPOFF); tft.sendCommand(ST77XX_SLPIN);}
    else {tft.sendCommand(ST77XX_SLPOUT); wakeAt=now+125; y=Height; dirty=true;}
  }
  void progress(uint8_t kind,uint16_t p) {
    if (kind!=holdKind || p/20!=hold/20) dirty=true;
    holdKind=kind; hold=p;
  }
  void tick(uint32_t now) {
    if(stopped) return;
    if(waking) {
      if(int32_t(now-wakeAt)<0) return;
      tft.sendCommand(ST77XX_DISPON);
      waking=false;
    }
    if(restartFrame) {y=Height; restartFrame=false;}
    if(y>=Height) {
      const bool animate=current.view==FaceView ||
        (current.view==QrView && d.side && now-d.qrSince<710) ||
        (current.view!=previous.view && now-change<art::SlideMs+85);
      if(!dirty && (!animate || now-lastDraw<55)) return;
      dirty=false; y=0; clock=lastDraw=now;
      drawn=current; under=previous; frameInward=inward; frameHold=hold; frameKind=holdKind;
      faceSince=d.faceSince; frameOldFace=oldFace;
      transition=current.view!=previous.view?min(now-change,uint32_t(art::SlideMs)):art::SlideMs;
    }
    if (transition<art::SlideMs) {
      const uint32_t p=uint32_t(transition)*256/art::SlideMs;
      const uint16_t q=(p*p*(768-2*p))>>16;
      const uint16_t a=frameInward?q:256-q;
      const int16_t moving=160*(256-a)/256;
      memset(line,0,sizeof line);
      scene(frameInward?under:drawn,-56*int16_t(a)/256);
      scene(frameInward?drawn:under,moving);
      if(moving>0 && moving<160) {line[moving-1]>>=1;}
    } else scene(drawn,0);
    shift=0;
    if (frameKind) {
      rect(4,96,152,19,art::bg);
      text(8,98,frameKind==9?PSTR("QR SESSION RESTART"):PSTR("POWER HOLD"),art::ink);
      rect(8,108,144,4,art::hdr); rect(8,108,144UL*frameHold/1000,4,art::acc);
    } else if ((drawn.view==FaceView || drawn.view==MenuView) && (d.fault||d.pending)) {
      rect(4,112,152,13,art::bg);
      text(8,115,d.fault?fault():PSTR("WAITING FOR CONTROLLER"),art::warn);
    }
    tft.startWrite(); tft.setAddrWindow(0,y,Width,1); tft.writePixels(line,Width); tft.endWrite();
    ++y;
  }
};
}

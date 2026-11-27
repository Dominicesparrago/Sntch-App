#include <cstdio>
#include <cstdlib>
#include <string>
#include <set>
#include <PanelButtons.h>
#include "dual_panel.h"
#include "solo_panel.h"
#include "PanelUi.h"
#include <SPI.h>

using namespace panel;
using namespace sntch;
uint32_t fakeNow=1000;
FakeUart Serial2;
FakeEsp ESP;
std::array<uint8_t,40> pinLevels{},pinModes{};
bool fakeTftInit=false;
SPIClass SPI;
struct Wire { std::vector<uint8_t> bytes; void write(uint8_t b) {bytes.push_back(b);} };

static unsigned checks=0;
#define CHECK(expr) do {++checks; if (!(expr)) {std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#expr); std::exit(1);} } while (0)

static bool decode(const std::vector<uint8_t>& bytes,Frame& f) {
  Reader reader;
  bool got=false;
  for(auto b:bytes) if(reader.feed(b,fakeNow,f)) { CHECK(!got); got=true; }
  return got;
}
static void framing() {
  const uint8_t text[]="123456789";
  CHECK(crc(text,9)==0x29b1);
  CHECK(newer(0,65535) && !newer(65535,0));
  Frame f,r; f.type=QrBits; f.seq=0x7d7e; f.size=Max;
  memset(f.data,0x7e,Max);
  Wire wire; send(wire,f);
  CHECK(decode(wire.bytes,r) && r.type==f.type && r.seq==f.seq && r.size==f.size);
  CHECK(!memcmp(r.data,f.data,Max));
  auto bad=wire.bytes; bad[2]^=1;
  CHECK(!decode(bad,r));
  auto over=wire.bytes; over.insert(over.begin()+8,80,0x42); CHECK(!decode(over,r));
  Reader reader;
  for(unsigned i=0;i<6;++i) CHECK(!reader.feed(wire.bytes[i],fakeNow,r));
  for(unsigned i=6;i<wire.bytes.size();++i) CHECK(!reader.feed(wire.bytes[i],fakeNow+81,r));
  bool got=false;
  for(const auto b:wire.bytes) if(reader.feed(b,fakeNow+82,r)) got=true;
  CHECK(got);
}
static void buttons() {
  Buttons b;
  uint32_t t=0;
  std::vector<Buttons::Event> e;
  auto scan=[&](uint8_t mask,uint32_t dt) {t+=dt;b.scan(mask,t,[&](auto event){e.push_back(event);});};
  auto set=[&](uint8_t mask) {scan(mask,1);scan(mask,DebounceMs);};
  scan(1,1); scan(0,10); // contact bounce
  set(1); set(0);
  CHECK(e==std::vector<Buttons::Event>{Buttons::Up});e.clear();
  set(2);scan(2,LongMs);set(0);
  CHECK(e==std::vector<Buttons::Event>{Buttons::LongDown});e.clear();
  set(4);scan(4,LongMs+100);set(12);
  scan(12,RestartMs-1);CHECK(e.empty() && b.progress(t)<1000);
  scan(12,1);scan(12,2000);
  CHECK(e==std::vector<Buttons::Event>{Buttons::RestartHold});
  set(4);set(12);scan(12,3000);set(0);CHECK(e.size()==1);e.clear();
  set(3);scan(3,1000);set(1);set(0);CHECK(e.empty() && !b.holding());
  set(3);scan(3,PowerMs-10);set(7);set(3);scan(3,PowerMs+10);set(0);CHECK(e.empty());
  for(int i=1;i<=2;++i) {set(3);scan(3,PowerMs);set(0);CHECK(e.size()==size_t(i) && e.back()==Buttons::PowerHold);}
}

static unsigned liveActivity(Classroom& r,const char* title) {
  Json item=Json::object(); item.set("title",Json::makeString(title)); item.set("target",Json::makeString("9-A"));
  unsigned id=0; CHECK(r.createActivity(item,id).ok); CHECK(r.publishItem("activity",id).ok);
  Json request=Json::object(); request.set("refType",Json::makeString("activity"));request.set("refId",Json(int(id)));
  unsigned sid=0; CHECK(r.createSession(request,fakeNow,sid).ok);
  CHECK(r.sessionAction(sid,"start",fakeNow).ok);
  return sid;
}

static void renderer() {
  Adafruit_ST7735 tft; panel_ui::Data data; panel_ui::Ui ui(tft,data);
  auto tick=[&](uint32_t t){for(uint8_t i=0;i<Height;++i)ui.tick(t);};
  std::set<uint32_t> sprites;
  for(uint8_t state=0;state<Faces;++state) {
    const uint32_t now=1000u*(state+1);
    data.faceSince=now; auto scene=ui.state(); scene.face=state;ui.set(scene,now);
    tick(now+400);
    uint32_t hash=0;
    for(unsigned y=0;y<Height;++y) for(auto p:tft.pixels[y]) hash=hash*31+p;
    sprites.insert(hash);
    CHECK(tft.pixels[0][0]==art::FaceBg);
  }
  CHECK(sprites.size()==Faces);
  auto scene=ui.state(); scene.view=panel_ui::MenuView;
  for(uint8_t i=0;i<9;++i) {
    scene.index=i;
    ui.set(scene,15000+i*1000); tick(15500+i*1000);
    CHECK(tft.pixels[0][0]==art::hdr);
    CHECK(tft.pixels[19+i*10][2]==art::acc);
  }
  ui.sleep(true,25000);CHECK(tft.lastCommand==ST77XX_SLPIN);
  const auto rows=tft.rows;tick(25000);CHECK(tft.rows==rows);
  ui.sleep(false,25000);tick(25124);CHECK(tft.rows==rows && tft.lastCommand==ST77XX_SLPOUT);
  tick(25125);CHECK(tft.rows==rows+Height && tft.lastCommand==ST77XX_DISPON);
}

static void exchange(const char* output) {
  MemoryStorage storage;
  CHECK(storage.write("/sntch_pending_init.txt","SNTCH-PENDING-INIT-V1"));
  Classroom classroom(&storage,"246810");
  CHECK(classroom.load()); classroom.setSeed(124502);
  sntch_fw::DualPanel first, restarted; auto* device=&first; device->begin();
  uint16_t seq=0; uint32_t cookie=0; bool wifi=true,service=true;
  auto request=[&](uint8_t type,std::initializer_list<uint8_t> payload={}) {
    Frame f; f.type=type;f.seq=++seq;f.size=4+payload.size();
    put32(f.data,type==Hello?12345:cookie);std::copy(payload.begin(),payload.end(),f.data+4);return f;
  };
  auto deliver=[&](const Frame& f) {
    Wire wire;send(wire,f);
    Serial2.input.insert(Serial2.input.end(),wire.bytes.begin(),wire.bytes.end());
    Serial2.output.clear(); fakeNow+=30;
    device->poll(classroom,wifi,service,true,2,31,"192.168.4.1","SNTCH-TEST");
    Frame ret;CHECK(decode(Serial2.output,ret)&&ret.seq==f.seq);return ret;
  };
  auto matrix=[&](Frame info,const char* name) {
    const uint8_t n=info.data[9]; const uint32_t generation=get32(info.data+5);
    CHECK(info.type==QrInfo && info.size==26 && qrSide(n) && qrScale(n)>=2);
    const uint16_t bytes=get16(info.data+10);CHECK(bytes==qrBytes(n));
    uint8_t packed[MatrixMax]={};
    for(uint16_t off=0;off<bytes;off+=Chunk) {
      Frame r=request(ReadQr);r.size=10;put32(r.data+4,generation);put16(r.data+8,off);
      const Frame b=deliver(r);
      const uint8_t count=min(uint16_t(Chunk),uint16_t(bytes-off));
      CHECK(b.type==QrBits&&b.size==10+count&&get32(b.data+4)==generation&&get16(b.data+8)==off);
      memcpy(packed+off,b.data+10,count);
    }
    CHECK(crc(packed,bytes)==get16(info.data+12));
    // Render over the actual UNO scanline path, with a host SPI sink.
    panel_ui::Data data;
    data.linked=true; data.kind=info.data[4]; data.side=n; data.qrSince=1000;
    memcpy(data.matrix,packed,bytes);
    memcpy(data.code,info.data+18,8);
    Adafruit_ST7735 tft; panel_ui::Ui ui(tft,data);
    panel_ui::Scene scene;scene.view=panel_ui::QrView;scene.index=data.kind;ui.set(scene,1000);
    for(uint8_t y=0;y<Height;++y)ui.tick(1800);
    const auto scale=qrScale(n);
    CHECK(tft.pixels[24][8]==0xffff);
    CHECK(tft.pixels[24+4*scale][8+4*scale]==art::dark);
    CHECK(tft.pixels[24+(n+7)*scale][8+(n+7)*scale]==0xffff);
    // Dump actual QR slot pixels from the AVR renderer. The independent
    // browser-test decoder reads these images without knowing the bit layout.
    if(output){
      std::string filename=std::string(output)+"/"+name+".qr";
      auto fp=std::fopen(filename.c_str(),"w");CHECK(fp!=nullptr);
      for(uint8_t y=0;y<(n+8)*scale;++y){
        for(uint8_t x=0;x<(n+8)*scale;++x)
          std::fputc(tft.pixels[24+y][8+x]==art::dark?'1':'0',fp);
        std::fputc('\n',fp);
      }
      fclose(fp);
    }
  };
  auto r=deliver(request(Hello)); CHECK(r.type==HelloAck && get32(r.data+4)==12345); cookie=get32(r.data);
  r=deliver(request(Status));CHECK(r.type==Snapshot && r.size==32 && (r.data[4]&Wifi) && r.data[5]==2 && r.data[8]==31);
  CHECK(r.data[19]==0);  // reserved slot in the v1 snapshot
  CHECK(deliver(request(Button,{6})).type==Ack);
  r=deliver(request(GetQr,{1}));CHECK(r.type==Error && r.data[4]==NoSession);
  const auto pair=deliver(request(GetQr,{0}));matrix(pair,"controller");
  const auto refresh=request(RestartQr,{0});
  const auto replaced=deliver(refresh);
  CHECK(replaced.type==QrInfo && get32(replaced.data+5)!=get32(pair.data+5));
  CHECK(deliver(request(Hello)).type==HelloAck); // Doesn't overwrite cached reply.
  const auto duplicate=deliver(refresh);
  CHECK(duplicate.size==replaced.size && !memcmp(duplicate.data,replaced.data,replaced.size));
  matrix(replaced,"refreshed");

  const unsigned sid=liveActivity(classroom,"Warm up");
  Json roster;
  CHECK(parseJson("{\"students\":[{\"number\":\"S-001\",\"name\":\"Student One\",\"section\":\"9-A\"}]}",roster));
  int added=0,updated=0;CHECK(classroom.addStudents(roster,added,updated).ok && added==1);
  const auto oldActivity=deliver(request(GetQr,{1}));
  CHECK(oldActivity.type==QrInfo && get32(oldActivity.data+14)==sid);
  const auto oldCode=classroom.sessions()[0].code;
  unsigned student=0,session=0;std::string token;
  CHECK(classroom.join(oldCode,"S-001",fakeNow,student,session,token).ok);
  Frame rotate=request(RestartQr,{1});rotate.size=9;put32(rotate.data+5,get32(oldActivity.data+5));
  auto newActivity=deliver(rotate); CHECK(newActivity.type==QrInfo);
  auto newCode=classroom.sessions()[0].code;CHECK(oldCode!=newCode);
  CHECK(deliver(rotate).type==QrInfo && classroom.sessions()[0].code==newCode);
  Json preview;CHECK(!classroom.sessionPreview(oldCode,fakeNow,preview).ok);
  CHECK(classroom.sessionPreview(newCode,fakeNow,preview).ok);
  CHECK(classroom.authorizeStudent(token,fakeNow,student,session).ok && session==sid);
  matrix(newActivity,"activity");
  Classroom stored(&storage,"246810");CHECK(stored.load() && stored.sessions()[0].code==newCode);
  rotate=request(RestartQr,{1});rotate.size=9;put32(rotate.data+5,get32(newActivity.data+5));
  storage.setFailWrites(true);r=deliver(rotate);CHECK(r.type==Error && r.data[4]==Store && classroom.sessions()[0].code==newCode);
  storage.setFailWrites(false);
  liveActivity(classroom,"Second activity");
  r=deliver(request(GetQr,{1}));CHECK(r.type==Error && r.data[4]==Ambiguous);
  wifi=false; r=deliver(request(GetQr,{0}));CHECK(r.type==Error && r.data[4]==Offline);
  r=deliver(request(Status));CHECK(!(r.data[4]&Wifi));
  wifi=true;
  CHECK(deliver(request(Standby)).type==Ack);
  CHECK(deliver(request(Status)).data[4]&Paused);
  CHECK(deliver(request(Wake)).type==Ack);
  CHECK(!(deliver(request(Status)).data[4]&Paused));
  device=&restarted;device->begin();
  r=deliver(request(RestartQr,{0})); CHECK(r.type==Error && r.data[4]==Reset);
  r=deliver(request(Hello));CHECK(r.type==HelloAck && get32(r.data)!=cookie);cookie=get32(r.data);
  CHECK(deliver(request(GetQr,{0})).type==QrInfo);
  Frame unoReset=request(Hello);put32(unoReset.data,54321);
  r=deliver(unoReset);CHECK(r.type==HelloAck && get32(r.data+4)==54321 && get32(r.data)!=cookie);
  r=deliver(request(Status));CHECK(r.type==Error && r.data[4]==Reset);
  std::puts("Dual UART exchange, authority, reset recovery and QR chunks passed");
}

static void integratedPanel() {
  using namespace panel_ui;
  MemoryStorage disk;
  CHECK(disk.write("/sntch_pending_init.txt","SNTCH-PENDING-INIT-V1"));
  Classroom room(&disk,"246810");
  CHECK(room.load()); room.setSeed(78901);
  pinLevels.fill(HIGH);
  sntch_fw::SoloPanel solo;
  solo.begin();
  CHECK(SPI.began && fakeTftInit && digitalRead(SoloTftCs)==HIGH);
  for(const auto pin:{SoloUp,SoloDown,SoloMenu,SoloSelect}) CHECK(pinModes.at(pin)==INPUT_PULLUP);
  bool wifi=true;
  auto step=[&](uint32_t dt,uint8_t keys=0) {
    fakeNow+=dt;
    unsigned i=0;
    for(auto p:{SoloUp,SoloDown,SoloMenu,SoloSelect}) pinLevels.at(p)=keys&(1<<i++)?LOW:HIGH;
    solo.poll(room,wifi,wifi,true,wifi?2:0,12,"192.168.4.1","SNTCH-SOLO");
  };
  auto settle=[&](uint8_t mask){step(1,mask);step(DebounceMs,mask);};
  auto key=[&](uint8_t mask){settle(mask);settle(0);};
  auto hold=[&](uint8_t mask,uint32_t time){settle(mask);step(time,mask);};
  step(3300);
  CHECK(solo.scene().view==FaceView && (solo.displayState().flags&Service) && solo.displayState().clients==2);
  key(4); CHECK(solo.scene().view==MenuView && solo.scene().index==0);
  key(2); key(8);
  CHECK(solo.scene().view==QrView && solo.scene().index==0 && qrSide(solo.displayState().side));
  const std::string oldOffer=solo.displayState().code;
  hold(12,RestartMs-1); settle(0);
  CHECK(solo.scene().view==QrView && solo.displayState().code==oldOffer);
  hold(12,RestartMs);
  const std::string newOffer=solo.displayState().code;
  CHECK(newOffer!=oldOffer);
  step(RestartMs+100,12); settle(0);
  CHECK(solo.displayState().code==newOffer);

  const unsigned id=liveActivity(room,"Integrated activity");
  Json roster;
  CHECK(parseJson("{\"students\":[{\"number\":\"S-001\",\"name\":\"Student One\",\"section\":\"9-A\"}]}",roster));
  int added=0,updated=0; CHECK(room.addStudents(roster,added,updated).ok);
  key(4); key(2); key(8);
  CHECK(solo.scene().view==QrView && solo.scene().index==1 &&
        solo.displayState().code==room.sessions()[0].code);
  const std::string priorCode=solo.displayState().code;
  unsigned student=0, session=0;
  std::string token;
  CHECK(room.join(priorCode,"S-001",fakeNow,student,session,token).ok && session==id);
  hold(12,RestartMs); settle(0);
  const std::string activeCode=solo.displayState().code;
  CHECK(activeCode!=priorCode && room.sessions()[0].code==activeCode);
  CHECK(room.authorizeStudent(token,fakeNow,student,session).ok);
  wifi=false; step(800);
  CHECK(!solo.displayState().side && solo.displayState().fault==Offline);
  wifi=true; step(1400);
  CHECK(qrSide(solo.displayState().side) && solo.displayState().code==activeCode);
  disk.setFailWrites(true);
  hold(3,PowerMs); settle(0);
  CHECK(solo.scene().view!=OffView && solo.displayState().fault==Store);
  disk.setFailWrites(false);
  hold(3,PowerMs); settle(0); step(680);
  CHECK(solo.scene().view==OffView && (solo.displayState().flags&Service) && (solo.displayState().flags&Paused));
  CHECK(room.authorizeStudent(token,fakeNow,student,session).ok && id==session);
  hold(3,PowerMs); settle(0);
  CHECK(solo.scene().view==FaceView && solo.scene().face==Recovery);
  // Explicit restart checkpoints and delays the hardware reboot.
  key(4); key(1); key(1); key(8);
  CHECK(solo.scene().view==DetailView && solo.scene().index==7);
  const auto resets=ESP.resets; key(8); step(351);
  CHECK(ESP.resets==resets+1);
  Classroom reloaded(&disk,"246810"); CHECK(reloaded.load() && reloaded.sessions()[0].code==activeCode);
  std::puts("Integrated ESP32 UI, QR authority, standby and restart passed");
}

int main(int argc,char** argv) {
  framing(); buttons(); renderer(); exchange(argc>1?argv[1]:nullptr); integratedPanel();
  std::printf("%u hardware-panel checks passed\n",checks);
}

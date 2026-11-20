/* Sticky PTP 4: build-only research firmware. No display, Wi-Fi, synthetic input.
 * GPIO/power sequence follows sketches/_common/sticky_hw.h (not included).
 * See docs/STICKY_PTP_BLE.md before any upload or pairing.
 */
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_bt.h>
#include <esp_pm.h>
#include <atomic>
#include "gt911_input.h"
#include "ptp_features.h"
#include "ptp_link.h"
using namespace stickyptp;

static portMUX_TYPE stateMux=portMUX_INITIALIZER_UNLOCKED;
static Link linkState;
static Features features;
static std::atomic<bool> notifyInFlight{false};
static uint32_t reads[7]{},writes[7]{},badFeatures=0,asyncErrors=0;
static NimBLEServer* server;
static NimBLEHIDDevice* hid;
static NimBLECharacteristic* touchReport;
static NimBLECharacteristic* featureChars[7]{};
static Core core;
static Touch touch;
static uint32_t successes=0,failures=0,discarded=0,sampleUs=0,lastPoll=0;
static uint32_t timingCount=0,timingMax=0;
static uint64_t timingSum=0;
static bool haveSample=false;
static char tx[1200]{};static size_t txSize=0,txPos=0;
static void bump(uint32_t& n){if(n!=UINT32_MAX)n++;}
static Link snapshot(){portENTER_CRITICAL(&stateMux);Link l=linkState;portEXIT_CRITICAL(&stateMux);return l;}
static void refreshFeature(uint8_t id){
 uint8_t bytes[33]{};portENTER_CRITICAL(&stateMux);size_t n=features.read(id,bytes);portEXIT_CRITICAL(&stateMux);
 if(n)featureChars[id]->setValue(bytes,n);
}
class ServerCallbacks final:public NimBLEServerCallbacks {
 void onConnect(NimBLEServer* s,NimBLEConnInfo& i)override{
   portENTER_CRITICAL(&stateMux);
   bool ready=linkState.touchReady;uint32_t epoch=linkState.epoch+1;
   linkState=Link{};linkState.touchReady=ready;linkState.epoch=epoch;
   linkState.connected=true;linkState.handle=i.getConnHandle();linkState.interval=i.getConnInterval();
   linkState.latency=i.getConnLatency();linkState.mtu=i.getMTU();
   linkState.bonded=i.isBonded();linkState.encrypted=i.isEncrypted();features.reset();
   portEXIT_CRITICAL(&stateMux);
   notifyInFlight.store(false);
   s->updateConnParams(i.getConnHandle(),6,6,0,300);
   NimBLEDevice::startSecurity(i.getConnHandle());
 }
 void onDisconnect(NimBLEServer*,NimBLEConnInfo&,int)override{
   portENTER_CRITICAL(&stateMux);
   linkState.connected=false;linkState.subscribed=false;linkState.encrypted=false;
   linkState.mode=0;linkState.epoch++;features.reset();
   portEXIT_CRITICAL(&stateMux);
   notifyInFlight.store(false);
   NimBLEDevice::startAdvertising();
 }
 void onAuthenticationComplete(NimBLEConnInfo& i)override{
   portENTER_CRITICAL(&stateMux);
   linkState.bonded=i.isBonded();linkState.encrypted=i.isEncrypted();linkState.epoch++;
   portEXIT_CRITICAL(&stateMux);
 }
 void onMTUChange(uint16_t mtu,NimBLEConnInfo&)override{
   portENTER_CRITICAL(&stateMux);
   if((linkState.mtu>=31)!=(mtu>=31))linkState.epoch++;
   linkState.mtu=mtu;portEXIT_CRITICAL(&stateMux);
 }
 void onConnParamsUpdate(NimBLEConnInfo& i)override{
   portENTER_CRITICAL(&stateMux);linkState.interval=i.getConnInterval();linkState.latency=i.getConnLatency();portEXIT_CRITICAL(&stateMux);
 }
};
class InputCallbacks final:public NimBLECharacteristicCallbacks {
 void onSubscribe(NimBLECharacteristic*,NimBLEConnInfo&,uint16_t value)override{
   portENTER_CRITICAL(&stateMux);bool subscribed=(value&1)!=0;
   if(subscribed!=linkState.subscribed){linkState.subscribed=subscribed;linkState.epoch++;}
   portEXIT_CRITICAL(&stateMux);
 }
 void onStatus(NimBLECharacteristic*,NimBLEConnInfo&,int code)override{
   notifyInFlight.store(false);
   if(code){portENTER_CRITICAL(&stateMux);bump(asyncErrors);portEXIT_CRITICAL(&stateMux);}
 }
};
class FeatureCallbacks final:public NimBLECharacteristicCallbacks {
 uint8_t idOf(NimBLECharacteristic* c){for(uint8_t id=2;id<=6;id++)if(c==featureChars[id])return id;return 0;}
 void onRead(NimBLECharacteristic* c,NimBLEConnInfo&)override{
   const auto id=idOf(c);if(!id)return;
   portENTER_CRITICAL(&stateMux);bump(reads[id]);portEXIT_CRITICAL(&stateMux);refreshFeature(id);
 }
 void onWrite(NimBLECharacteristic* c,NimBLEConnInfo&)override{
   const auto id=idOf(c);if(!id)return;
   auto v=c->getValue();
   portENTER_CRITICAL(&stateMux);bump(writes[id]);
   auto mode=features.mode,sel=features.selective;
   if(!features.write(id,v.data(),v.size()))bump(badFeatures);
   linkState.mode=features.mode;linkState.selective=features.selective;
   if(mode!=features.mode || sel!=features.selective)linkState.inputEpoch++;
   portEXIT_CRITICAL(&stateMux);refreshFeature(id);
 }
};
class ControlCallbacks final:public NimBLECharacteristicCallbacks {
 void onWrite(NimBLECharacteristic* c,NimBLEConnInfo&)override{
   auto v=c->getValue();if(v.size()!=1 || v[0]>1)return;
   portENTER_CRITICAL(&stateMux);
   if(c==hid->getHidControl()){
     bool suspended=v[0]==0;if(suspended!=linkState.suspended)linkState.epoch++;linkState.suspended=suspended;
   }else{
     bool report=v[0]==1;if(report!=linkState.reportProtocol)linkState.epoch++;linkState.reportProtocol=report;
   }
   portEXIT_CRITICAL(&stateMux);
 }
};
static ServerCallbacks serverCB;static InputCallbacks inputCB;static FeatureCallbacks featureCB;static ControlCallbacks controlCB;

static void queueStatus(){
 if(txPos<txSize)return;
 Link l;Features f;uint32_t rd[7],wr[7],bad,ae;
 portENTER_CRITICAL(&stateMux);l=linkState;f=features;
 for(unsigned i=0;i<7;i++){rd[i]=reads[i];wr[i]=writes[i];}bad=badFeatures;ae=asyncErrors;
 portEXIT_CRITICAL(&stateMux);
 int n=snprintf(tx,sizeof(tx),
 "Sticky PTP 4 up_ms=%lu touch=%u addr=0x%02x connected=%u bonded=%u encrypted=%u sub=%u ready=%u enabled=%u gate=400ms mode=%u sel=%u latencyFeature=%u hqa=%u\n"
 "interval_us=%lu slave_latency=%u mtu=%u mtu_block=%u (PTP requires >=31) suspend=%u reportProtocol=%u\n"
 "polls=%lu frames=%lu invalid=%lu i2c_errors=%lu notify_ok=%lu notify_fail=%lu async_errors=%lu epoch_discard=%lu\n"
 "sample_to_submit_us n=%lu mean=%lu max=%lu (device software only, not end-to-end)\n"
 "features caps R/W=%lu/%lu hqa=%lu/%lu mode=%lu/%lu selective=%lu/%lu latency=%lu/%lu invalid=%lu\n",
 (unsigned long)millis(),l.touchReady,touch.address,l.connected,l.bonded,l.encrypted,l.subscribed,l.transportReady(),l.inputEnabled(),f.mode,f.selective,f.latency,f.segment,
 (unsigned long)intervalUs(l.interval),l.latency,l.mtu,l.mtu<31,l.suspended,l.reportProtocol,
 (unsigned long)touch.polls,(unsigned long)touch.frames,(unsigned long)touch.invalid,(unsigned long)touch.errors,(unsigned long)successes,(unsigned long)failures,(unsigned long)ae,(unsigned long)discarded,
 (unsigned long)timingCount,(unsigned long)(timingCount?timingSum/timingCount:0),(unsigned long)timingMax,
 (unsigned long)rd[2],(unsigned long)wr[2],(unsigned long)rd[3],(unsigned long)wr[3],(unsigned long)rd[4],(unsigned long)wr[4],(unsigned long)rd[5],(unsigned long)wr[5],(unsigned long)rd[6],(unsigned long)wr[6],(unsigned long)bad);
 txSize=n>0?size_t(n):0;if(txSize>=sizeof(tx))txSize=sizeof(tx)-1;txPos=0;
}
static void console(){
 static char cmd[24]{};static uint8_t len=0;static bool overflow=false;
 for(unsigned budget=0;budget<16 && Serial.available();budget++){
   char ch=char(Serial.read());
   if(ch=='\n'||ch=='\r'){
     if(!overflow && len){cmd[len]=0;
       if(!strcmp(cmd,"STATUS"))queueStatus();
       else if(!strcmp(cmd,"PING") && txPos==txSize){strcpy(tx,"PONG Sticky PTP 4\n");txSize=strlen(tx);txPos=0;}
     }len=0;overflow=false;
   }else if(!overflow){if(len<sizeof(cmd)-1)cmd[len++]=ch;else overflow=true;}
 }
 size_t n=txSize-txPos;int avail=Serial.availableForWrite();if(n>32)n=32;
 if(avail>0 && n){if(n>size_t(avail))n=size_t(avail);txPos+=Serial.write(reinterpret_cast<const uint8_t*>(tx+txPos),n);}
}
void setup(){
 pinMode(45,OUTPUT);digitalWrite(45,HIGH);pinMode(46,OUTPUT);digitalWrite(46,HIGH);
 Serial.begin(115200);setCpuFrequencyMhz(240);
 linkState.touchReady=touch.begin();
 NimBLEDevice::init("Sticky PTP 4");NimBLEDevice::setMTU(64);
 NimBLEDevice::setSecurityAuth(true,false,true);NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
 // No TX-power override. Disable controller modem sleep, not connection scheduling.
 const esp_err_t sleepResult=esp_bt_sleep_disable();
 (void)sleepResult;
 #if CONFIG_PM_ENABLE
 static esp_pm_lock_handle_t cpuLock,noSleepLock;
 if(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"ptp_cpu",&cpuLock)==ESP_OK)esp_pm_lock_acquire(cpuLock);
 if(esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"ptp_awake",&noSleepLock)==ESP_OK)esp_pm_lock_acquire(noSleepLock);
 #endif
 server=NimBLEDevice::createServer();server->setCallbacks(&serverCB,false);
 hid=new NimBLEHIDDevice(server);hid->setManufacturer("Sticky PTP research prototype");
 // All-zero, unassigned prototype PnP tuple; no claim to a registered vendor ID.
 hid->setPnp(0,0,0,0x0001);hid->setHidInfo(0,1);
 hid->setReportMap(const_cast<uint8_t*>(ptpReportDescriptor),sizeof(ptpReportDescriptor));
 touchReport=hid->getInputReport(RID_TOUCHPAD);touchReport->setCallbacks(&inputCB);
 for(uint8_t id=2;id<=6;id++){featureChars[id]=hid->getFeatureReport(id);featureChars[id]->setCallbacks(&featureCB);refreshFeature(id);}
 hid->getHidControl()->setCallbacks(&controlCB);hid->getProtocolMode()->setCallbacks(&controlCB);
 server->start();auto adv=NimBLEDevice::getAdvertising();adv->setName("Sticky PTP 4");adv->setAppearance(0x03c2);
 adv->addServiceUUID(hid->getHidService()->getUUID());adv->enableScanResponse(true);NimBLEDevice::startAdvertising();
 core.reset(micros());
 snprintf(tx,sizeof(tx),"Sticky PTP 4 research; GT911=%s addr=0x%02x I2C=%u timeout=%ums BTsleepDisable=%d; STATUS/PING 115200; no display refresh\n",linkState.touchReady?"911":"FAILED",touch.address,STICKY_PTP_I2C_HZ,STICKY_PTP_I2C_TIMEOUT_MS,int(sleepResult));
 txSize=strlen(tx);txPos=0;
}
void loop(){
 static uint32_t seenEpoch=UINT32_MAX,seenInputEpoch=UINT32_MAX;static bool wasReady=false,wasEnabled=false;
 Link l=snapshot();uint32_t now=micros();bool ready=l.transportReady(),enabled=l.inputEnabled();
 if(l.epoch!=seenEpoch || ready!=wasReady){
   core.reset(now);core.setEnabled(enabled,now);haveSample=false;
   seenEpoch=l.epoch;seenInputEpoch=l.inputEpoch;wasReady=ready;wasEnabled=enabled;
   // Deferred transition/low-MTU logging: never print in BLE callbacks.
   queueStatus();
 }else if(l.inputEpoch!=seenInputEpoch || enabled!=wasEnabled){
   // Preserve acknowledged contacts as pending releases on mode/selective disable.
   core.setEnabled(enabled,now);haveSample=false;seenInputEpoch=l.inputEpoch;wasEnabled=enabled;
 }
 if(uint32_t(now-lastPoll)>=1000){
   lastPoll=now;Contact c[4]{};int n=touch.poll(c);uint32_t sampled=micros();
   if(n>=0 && core.sample(c,uint8_t(n),sampled)){sampleUs=sampled;haveSample=true;}
 }
 now=micros();Report report{};
 if(ready && !notifyInFlight.load() && core.prepare(now,intervalUs(l.interval),report)){
   // Snapshot once more before notify: changed epochs invalidate in-flight work.
   Link before=snapshot();
   if(before.epoch!=l.epoch || before.inputEpoch!=l.inputEpoch){bump(discarded);}
   else {
     touchReport->setValue(reinterpret_cast<const uint8_t*>(&report),sizeof(report));
     notifyInFlight.store(true); // onStatus may run before notify returns
     bool ok=touchReport->notify(reinterpret_cast<const uint8_t*>(&report),sizeof(report),l.handle);
     if(!ok)notifyInFlight.store(false);
     uint32_t submitted=micros();Link after=snapshot();
     if(ok)bump(successes);else bump(failures);
     if(after.epoch==l.epoch && after.inputEpoch==l.inputEpoch)core.complete(ok,submitted);
     else bump(discarded);
     if(ok && haveSample && timingCount<1000000){
       uint32_t elapsed=submitted-sampleUs;timingSum+=elapsed;bump(timingCount);if(elapsed>timingMax)timingMax=elapsed;
     }
   }
 }
 console();delay(1); // yield to BLE; never drain a backlog or catch up after a gap
}

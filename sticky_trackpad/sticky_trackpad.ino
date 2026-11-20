/*
 * Seeed reTerminal Sticky trackpad v0.1
 * Board-specific touch/display with BLE transport adapted from the related
 * M5PaperS3 v1.0. Original PaperS3 and touch_probe remain unchanged.
 * Generic mouse/keyboard HID gestures, NOT Microsoft Precision Touchpad.
 * Stock USB-C is CH343P UART0; native USB HID cannot reach that connector.
 * Starts PAUSED at every boot. AI short press toggles output; STOP always
 * releases buttons/modifiers. UP/DOWN select trackpad/numpad/keyboard/settings.
 * No synthetic HID, no touch-controller config overrides, no sleep.
 */
#include <Arduino.h>
#include <Preferences.h>
#include "BleTransport.h"
#include "StickyTouch.h"
#include "StickyDisplay.h"
#include "GestureEngine.h"
#include "UIModel.h"
#include "AppPolicy.h"

static BleTrackpadTransport BleHID;
static StickyTouch Touch;
static StickyDisplay Display;
static stickyapp::GestureEngine Engine;
static stickyapp::ArmGate Gate;
static StickyTouch::Frame rawFrame;
static stickyapp::Contact contacts[5];
static uint8_t screen=0, rotation=0;
static bool shifted=false, touchOK=false, displayDirty=true, trace=false;
static uint32_t lastGood=0,lastPoll=0,lastHeartbeat=0,lastUiRequest=0;
static uint32_t readErrors=0,frameCount=0,actionCount=0;
static uint8_t peakContacts=0;
static bool uiDown=false,uiInvalid=false;
static uint32_t uiStart=0;
static float uiX=0,uiY=0,uiTravel=0;
static int uiIndex=-1;
static uint32_t forgetDeadline=0;
static char commandBuffer[64];
static size_t commandLength=0;
static bool commandOverflow=false;

static void requestDraw() {displayDirty=true;lastUiRequest=millis();}
static void cancelInput() {
  Engine.cancel();
  if(rawFrame.count==0) Engine.update(nullptr,0,millis());
  BleHID.releaseAll();
  Gate.cancelUntilRelease();
  uiDown=false;uiInvalid=false;
}
static void setArmed(bool value) {
  cancelInput(); Gate.setArmed(value);
  if(rawFrame.count==0) Gate.permit(BleHID.isReady(),0);
  Serial.printf("[ARM] %s - release all fingers before next gesture\n",value?"ARMED":"PAUSED");
  requestDraw();
}
static void selectScreen(int next) {
  cancelInput();screen=(next+4)%4;shifted=false;
  Serial.printf("[UI] screen=%u\n",screen);requestDraw();
}
static void status() {
  Serial.printf("STATUS StickyTrackpad v0.1 touch=%u addr=0x%02X sensor=%ux%u display=%u screen=%u rotation=%u armed=%u connected=%u ready=%u bonds=%d frames=%lu count=%u peak=%u read_errors=%lu actions=%lu reports=%lu USB=UART_ONLY\n",
    touchOK,Touch.address(),Touch.sensorWidth(),Touch.sensorHeight(),Display.available(),screen,rotation,
    Gate.armed(),BleHID.isConnected(),BleHID.isReady(),BleHID.bondCount(),
    (unsigned long)frameCount,rawFrame.count,peakContacts,(unsigned long)readErrors,
    (unsigned long)actionCount,(unsigned long)BleHID.reportsSent());
}
static void execute(const stickyapp::Actions& actions) {
  for(uint8_t i=0;i<actions.count;++i) {
    auto a=actions.items[i];++actionCount;
    if(trace || a.type!=stickyapp::Move)
      Serial.printf("[GESTURE] type=%u a=%d b=%d\n",unsigned(a.type),a.a,a.b);
    switch(a.type) {
      case stickyapp::Move:BleHID.mouseMove(a.a,a.b,0,0);break;
      case stickyapp::Scroll: {
        auto wheel=stickyapp::wheelDispatch(a.a,a.b);
        BleHID.mouseMove(0,0,wheel.wheel,wheel.pan);break;
      }
      case stickyapp::Click:BleHID.mouseClick(a.a);break;
      case stickyapp::Press:BleHID.mousePress(a.a);break;
      case stickyapp::Release:BleHID.mouseRelease(a.a);break;
      case stickyapp::Zoom:
        BleHID.keyPress(0x80); // PaperS3's modifier convention: left control
        BleHID.mouseMove(0,0,a.a,0); BleHID.service(millis(),true);
        delay(12);BleHID.keyRelease(0x80);break;
      case stickyapp::SwipeLeft:BleHID.typeKey(0x50,0x82);break; // Alt+Left browser back
      case stickyapp::SwipeRight:BleHID.typeKey(0x4F,0x82);break;// Alt+Right forward
      case stickyapp::SwipeUp:BleHID.typeKey(0x2B,0x83);break; // GUI+Tab task overview
      case stickyapp::SwipeDown:BleHID.typeKey(0x07,0x83);break;// GUI+D desktop
    }
  }
}
static void processUi(uint32_t now) {
  if(rawFrame.count) {
    if(!uiDown) {
      uiDown=true;uiInvalid=false;uiStart=now;uiX=contacts[0].x;uiY=contacts[0].y;uiTravel=0;
      uiIndex=stickyapp::hitKey(static_cast<stickyapp::Screen>(screen),int(uiX),int(uiY));
    }
    if(rawFrame.count!=1) uiInvalid=true;
    if(rawFrame.count==1) {
      float dx=contacts[0].x-uiX,dy=contacts[0].y-uiY;
      uiTravel=std::max(uiTravel,dx*dx+dy*dy);
    }
  } else if(uiDown) {
    uiDown=false;
    if(uiInvalid || uiTravel>14*14 || now-uiStart>350 || uiIndex<0) return;
    auto key=stickyapp::keyAt(static_cast<stickyapp::Screen>(screen),uiIndex,shifted);
    if(key.usage==0xFF) {shifted=!shifted;requestDraw();}
    else if(key.usage) {
      BleHID.typeKey(key.usage,key.modifiers?0x81:0);
      Serial.printf("[KEY] usage=0x%02X modifier=0x%02X\n",key.usage,key.modifiers);
      if(shifted) {shifted=false;requestDraw();}
    }
  }
}
static void command(const char* s) {
  if(!strcmp(s,"PING")) Serial.println("PONG StickyTrackpad v0.1");
  else if(!strcmp(s,"STATUS")) status();
  else if(!strcmp(s,"ARM")) setArmed(true);
  else if(!strcmp(s,"STOP") || !strcmp(s,"PAUSE")) {setArmed(false);Serial.println("OK STOP RELEASED");}
  else if(!strcmp(s,"NEXT")) selectScreen(screen+1);
  else if(!strcmp(s,"PREV")) selectScreen(screen-1);
  else if(!strncmp(s,"SCREEN ",7) && strlen(s)==8 && s[7]>='0' && s[7]<='3') selectScreen(s[7]-'0');
  else if(!strncmp(s,"ROTATE ",7) && strlen(s)==8 && s[7]>='0' && s[7]<='3') {
    setArmed(false);rotation=s[7]-'0';Preferences p;p.begin("sticky_ui",false);p.putUChar("rot",rotation);p.end();
    Serial.printf("OK ROTATE %u\n",rotation);requestDraw();
  }
  else if(!strcmp(s,"FORGET")) {forgetDeadline=millis()+5000;setArmed(false);Serial.println("CONFIRM with FORGET YES within 5 seconds");}
  else if(!strcmp(s,"FORGET YES") && forgetDeadline && int32_t(forgetDeadline-millis())>0) {
    forgetDeadline=0;setArmed(false);BleHID.forgetAllAndPair();Serial.println("OK FORGET");requestDraw();
  }
  else if(!strcmp(s,"TRACE ON")) {trace=true;Serial.println("OK TRACE ON");}
  else if(!strcmp(s,"TRACE OFF")) {trace=false;Serial.println("OK TRACE OFF");}
  else if(!strcmp(s,"HELP")) Serial.println("PING STATUS ARM STOP SCREEN 0..3 NEXT PREV ROTATE 0..3 FORGET / FORGET YES TRACE ON/OFF");
  else Serial.println("ERR unknown command (HELP)");
}
static void serialCommands() {
  while(Serial.available()) {
    char c=char(Serial.read());
    if(c=='\r') continue;
    if(c=='\n') {
      commandBuffer[commandLength]=0;
      if(commandOverflow) Serial.println("ERR command too long");else if(commandLength) command(commandBuffer);
      commandLength=0;commandOverflow=false;
    } else if(commandLength<sizeof(commandBuffer)-1 && !commandOverflow) commandBuffer[commandLength++]=c;
    else commandOverflow=true;
  }
}
struct ButtonState {int pin;bool raw=true,stable=true;uint32_t changed=0;};
static ButtonState buttons[]={{4},{5},{6}};
static void pollButtons(uint32_t now) {
  for(auto& b:buttons) {
    bool level=digitalRead(b.pin);
    if(level!=b.raw) {b.raw=level;b.changed=now;}
    if(now-b.changed>=35 && b.stable!=b.raw) {
      b.stable=b.raw;
      if(!b.stable) {
        if(b.pin==4) setArmed(!Gate.armed());
        else if(b.pin==5) selectScreen(screen-1);
        else selectScreen(screen+1);
      }
    }
  }
}
void setup() {
  // Board power latch: match Seeed's peripheral demo; no battery/sleep changes.
  pinMode(45,OUTPUT);digitalWrite(45,HIGH);pinMode(46,OUTPUT);digitalWrite(46,HIGH);
  Serial.begin(115200);delay(800);
  Serial.println("StickyTrackpad v0.1 boot - PAUSED - USB UART only, BLE HID");
  for(auto& b:buttons) pinMode(b.pin,INPUT_PULLUP);
  Preferences p;p.begin("sticky_ui",true);rotation=p.getUChar("rot",0)&3;p.end();
  touchOK=Touch.begin();lastGood=millis();
  Serial.printf("Touch initialization: %s\n",touchOK?"OK":"FAILED");
  bool displayOK=Display.begin();
  Serial.printf("Display initialization: %s\n",displayOK?"OK":"FAILED");
  bool bleOK=BleHID.begin("Sticky Trackpad");
  Serial.printf("BLE initialization: %s\n",bleOK?"OK":"FAILED");
  Serial.printf("[BLE] address=%s\n",NimBLEDevice::getAddress().toString().c_str());
  if(displayOK) Display.draw(screen,false,false,rotation,false);
  displayDirty=false;status();
}
void loop() {
  uint32_t now=millis();
  serialCommands();pollButtons(now);BleHID.service(now);
  static uint32_t revision=0;
  if(revision!=BleHID.stateRevision()) {revision=BleHID.stateRevision();cancelInput();requestDraw();}
  if(touchOK && now-lastPoll>=5) {
    lastPoll=now;auto result=Touch.read(rawFrame);
    if(result==StickyTouch::Error) {++readErrors;cancelInput();}
    else if(result==StickyTouch::NewFrame) {
      lastGood=now;++frameCount;peakContacts=std::max(peakContacts,rawFrame.count);
      for(uint8_t i=0;i<rawFrame.count;++i) {
        auto xy=stickyapp::mapRaw(rawFrame.points[i].x,rawFrame.points[i].y,Touch.sensorWidth(),Touch.sensorHeight(),rotation);
        contacts[i]={rawFrame.points[i].id,xy.x,xy.y};
      }
      if(Gate.permit(BleHID.isReady(),rawFrame.count)) {
        if(screen==0) execute(Engine.update(contacts,rawFrame.count,now));
        else if(screen==1 || screen==2) processUi(now);
      }
      static uint32_t lastTrace=0;
      if(trace && (now-lastTrace>=30 || rawFrame.count==0)) {
        lastTrace=now;
        Serial.printf("TOUCH %s t=%lu n=%lu status=0x%02X count=%u",rawFrame.count?"MOVE":"UP",(unsigned long)now,(unsigned long)frameCount,0x80|rawFrame.count,rawFrame.count);
        for(uint8_t i=0;i<rawFrame.count;++i) {auto p=rawFrame.points[i];Serial.printf(" [id=%u x=%u y=%u size=%u]",p.id,p.x,p.y,p.size);}
        Serial.println();
      }
    }
  }
  // Loss of data must release held outputs, not fabricate a tap/release gesture.
  static bool staleCancelled=false;
  if(rawFrame.count>0 && now-lastGood>350) {if(!staleCancelled) {cancelInput();staleCancelled=true;}}
  else staleCancelled=false;
  if(displayDirty && rawFrame.count==0 && now-lastUiRequest>200) {
    cancelInput();
    bool ok=Display.draw(screen,Gate.armed(),BleHID.isReady(),rotation,shifted);
    Serial.printf("[DISPLAY] screen=%u refresh=%s\n",screen,ok?"OK":"FAILED");
    displayDirty=false;lastGood=millis();
    Gate.permit(BleHID.isReady(),0);
  }
  if(now-lastHeartbeat>=2000) {lastHeartbeat=now;status();}
  delay(1);
}

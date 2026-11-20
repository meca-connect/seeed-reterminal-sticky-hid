/*
 * s1_touch_blink — the smallest possible touch->display loop.
 *
 * Goal: prove touch reads AND e-paper refreshes in ONE observable step.
 * On any new touch-down, invert the whole screen once. No BLE, no gestures,
 * no UI, no coordinate mapping. Serial reports what happened.
 *
 * Touch: GT911 I2C0 SDA=3 SCL=2 INT=21 RST=41 EN=42, addr 0x5D, sensor 480x800.
 * Display: SSD1677 EN=47 RST=17 BUSY=18 CS=15 DC=16 MOSI=14 SCK=13 (SD CS8 HIGH).
 * Board power hold GPIO45/46. FQBN: esp32:esp32:esp32s3:FlashSize=32M,PSRAM=opi,
 * CDCOnBoot=default,USBMode=hwcdc,FlashMode=dio
 */
#include <Arduino.h>
#include <Wire.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

// ---------- touch (minimal GT911, same registers as the working probe) -------
static constexpr int T_SDA=3, T_SCL=2, T_INT=21, T_RST=41, T_EN=42;
static uint8_t gtAddr=0;

static bool readReg(uint16_t reg, uint8_t* out, size_t n){
  Wire.beginTransmission(gtAddr); Wire.write(uint8_t(reg>>8)); Wire.write(uint8_t(reg));
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom(int(gtAddr),int(n))!=n){while(Wire.available())Wire.read();return false;}
  for(size_t i=0;i<n;++i) out[i]=uint8_t(Wire.read());
  return true;
}
static bool writeReg(uint16_t reg, uint8_t v){
  Wire.beginTransmission(gtAddr); Wire.write(uint8_t(reg>>8)); Wire.write(uint8_t(reg)); Wire.write(v);
  return Wire.endTransmission()==0;
}
static bool touchBegin(){
  pinMode(T_EN,OUTPUT); digitalWrite(T_EN,HIGH); delay(300);
  if(!Wire.begin(T_SDA,T_SCL,100000)) return false;
  Wire.setTimeOut(20);
  for(uint8_t a : {uint8_t(0x5D),uint8_t(0x14)}){
    pinMode(T_RST,OUTPUT); digitalWrite(T_RST,LOW);
    pinMode(T_INT,OUTPUT); digitalWrite(T_INT, a==0x5D?LOW:HIGH);
    delay(20); digitalWrite(T_RST,HIGH); delay(20);
    digitalWrite(T_INT,LOW); delay(50); pinMode(T_INT,INPUT); delay(30);
    gtAddr=a;
    uint8_t id[4]{};
    if(readReg(0x8140,id,4) && id[0]=='9'&&id[1]=='1'&&id[2]=='1'&&id[3]==0){
      Serial.printf("[S1] GT911 addr=0x%02X\n",a);
      return writeReg(0x814E,0);
    }
  }
  gtAddr=0; return false;
}
// Returns true on a NEW touch-down frame.
static bool touchDown(){
  if(!gtAddr) return false;
  uint8_t st=0; if(!readReg(0x814E,&st,1)) return false;
  if(!(st&0x80)) return false;
  uint8_t count=st&15;
  if(count && count<=5){ uint8_t raw[40]{}; if(!readReg(0x814F,raw,count*8)) return false; }
  writeReg(0x814E,0);
  return count>0;
}

// ---------- display (minimal SSD1677 full refresh) ---------------------------
static constexpr int D_EN=47,D_RST=17,D_BUSY=18,D_CS=15,D_DC=16,D_MOSI=14,D_SCK=13,D_SDCS=8;
static constexpr int W=800,H=480, STRIDE=W/8, FRAME=STRIDE*H;
static uint8_t* fb=nullptr;
static spi_device_handle_t spi=nullptr;
static bool dReady=false;

static bool waitBusy(uint32_t timeoutMs=10000){
  uint32_t start=millis();
  while(digitalRead(D_BUSY)==HIGH){ if(uint32_t(millis()-start)>=timeoutMs){Serial.println("[S1] BUSY timeout");return false;} delay(1);}
  return true;
}
static bool spiCmd(uint8_t c){ return spiCmdData(false,&c,1); }
static bool spiData(const uint8_t* b,size_t n){ return spiCmdData(true,b,n); }
static bool spiCmdData(bool data,const uint8_t* b,size_t n){
  if(!spi) return false;
  digitalWrite(D_SDCS,HIGH);
  digitalWrite(D_DC, data?HIGH:LOW);
  for(size_t off=0; off<n; off+=1024){
    size_t chunk=(n-off)>1024?1024:(n-off);
    spi_transaction_t t={};
    t.length=chunk*8; t.tx_buffer=b+off;
    if(spi_device_polling_transmit(spi,&t)!=ESP_OK){Serial.println("[S1] SPI fail");return false;}
  }
  return true;
}
static bool reg8(uint8_t c,uint8_t v){ return spiCmd(c)&&spiData(&v,1); }
static bool displayBegin(){
  pinMode(D_SDCS,OUTPUT); digitalWrite(D_SDCS,HIGH);
  pinMode(D_CS,OUTPUT); digitalWrite(D_CS,HIGH);
  pinMode(D_DC,OUTPUT); digitalWrite(D_DC,LOW);
  pinMode(D_RST,OUTPUT); digitalWrite(D_RST,HIGH);
  pinMode(D_BUSY,INPUT);
  pinMode(D_EN,OUTPUT); digitalWrite(D_EN,HIGH);
  delay(100);
  spi_bus_config_t bus={};
  bus.mosi_io_num=D_MOSI; bus.miso_io_num=-1; bus.sclk_io_num=D_SCK;
  bus.quadwp_io_num=-1; bus.quadhd_io_num=-1; bus.max_transfer_sz=4096;
  if(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK) return false;
  spi_device_interface_config_t dev={};
  dev.clock_speed_hz=10000000; dev.mode=0; dev.spics_io_num=D_CS; dev.queue_size=1;
  if(spi_bus_add_device(SPI2_HOST,&dev,&spi)!=ESP_OK) return false;
  fb=(uint8_t*)heap_caps_malloc(FRAME, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!fb) fb=(uint8_t*)heap_caps_malloc(FRAME, MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if(!fb) return false;
  memset(fb,0xFF,FRAME);

  digitalWrite(D_RST,LOW); delay(10); digitalWrite(D_RST,HIGH); delay(10);
  if(!waitBusy()) return false;
  if(!spiCmd(0x12)||!waitBusy()) return false;             // soft reset
  const uint8_t softstart[]={0xAE,0xC7,0xC3,0xC0,0x80};
  const uint8_t driverout[]={0xDF,0x01,0x02};              // 479 rows
  const uint8_t xwin[]={0,0,0x1F,0x03};                    // 0..799
  const uint8_t ywin[]={0,0,0xDF,0x01};                    // 0..479
  const uint8_t zero[]={0,0};
  dReady = reg8(0x18,0x80) && reg8(0x3C,0x01) &&
           spiCmd(0x0C)&&spiData(softstart,sizeof(softstart)) &&
           spiCmd(0x01)&&spiData(driverout,sizeof(driverout)) && reg8(0x11,0x03) &&
           spiCmd(0x44)&&spiData(xwin,sizeof(xwin)) &&
           spiCmd(0x45)&&spiData(ywin,sizeof(ywin)) &&
           spiCmd(0x4E)&&spiData(zero,sizeof(zero)) &&
           spiCmd(0x4F)&&spiData(zero,sizeof(zero)) && waitBusy();
  return dReady;
}
static bool refreshFull(){
  if(!dReady) return false;
  spiCmd(0x24) && spiData(fb,FRAME);         // RAM BW
  reg8(0x22,0xF7) && spiCmd(0x20) && waitBusy();  // master activate
  return true;
}

// ---------- main ------------------------------------------------------------
static uint32_t lastFlash=0, downs=0;

void setup(){
  pinMode(45,OUTPUT); digitalWrite(45,HIGH);  // board power hold
  pinMode(46,OUTPUT); digitalWrite(46,HIGH);
  Serial.begin(115200); delay(800);
  Serial.println("S1 TOUCH BLINK — touch the screen to invert it (black<->white)");
  Serial.printf("Touch: %s\n", touchBegin()?"OK":"FAILED");
  Serial.printf("Display: %s\n", displayBegin()?"OK":"FAILED");
  refreshFull();
  Serial.println("READY — screen should be white. Touch to flash.");
}
void loop(){
  if(touchDown()){
    ++downs;
    // Invert every byte: white<->black across the whole panel.
    for(int i=0;i<FRAME;++i) fb[i]=(uint8_t)~fb[i];
    bool ok=refreshFull();
    Serial.printf("BLINK t=%lu downs=%lu refresh=%s\n",(unsigned long)millis(),(unsigned long)downs,ok?"OK":"FAILED");
    while(touchDown()) delay(10); // wait for release before accepting next
  }
  delay(5);
}

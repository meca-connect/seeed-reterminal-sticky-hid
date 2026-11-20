/*
 * Shared minimal Sticky touch + display drivers for the incremental sketches.
 * v2: corrected for the panel's real geometry, verified by s2b + s3_mirror.
 *
 * PHYSICAL / LOGICAL space (USB-C at the BOTTOM): PORTRAIT, 480 wide x 800 tall,
 * origin top-left. x = 0..479 left->right, y = 0..799 top->bottom.
 *
 * Touch GT911 (SDA=3 SCL=2 INT=21 RST=41 EN=42, addr 0x5D): reports 480x800.
 * raw x is already left->right, but raw y increases toward the PHYSICAL TOP
 * (raw y=0 at the USB end), so stickyTouchRead flips it: logical_y = 799 - raw_y.
 *
 * Display SSD1677 (EN=47 RST=17 BUSY=18 CS=15 DC=16 MOSI=14 SCK=13): the 800x480
 * panel is mounted ROTATED 90 DEGREES (800 columns run top->bottom). A logical
 * pixel (x,y) therefore lives at SSD (X = y, Y = x). drawPixel applies that
 * transpose directly; refresh sends the buffer unmirrored.
 *
 * Board power hold GPIO45/46. FQBN: esp32:esp32:esp32s3:FlashSize=32M,PSRAM=opi,
 * CDCOnBoot=default,USBMode=hwcdc,FlashMode=dio
 */
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>

static constexpr int LW=480, LH=800;              // logical/portrait
static constexpr int SSD_W=800, SSD_H=480;        // SSD1677 native layout
static constexpr int SSD_STRIDE=SSD_W/8, SSD_FRAME=SSD_STRIDE*SSD_H;

// ---------------- touch ----------------
static uint8_t sTouchAddr = 0;

static bool sTouchReadReg(uint16_t reg, uint8_t* out, size_t n) {
  Wire.beginTransmission(sTouchAddr); Wire.write(uint8_t(reg>>8)); Wire.write(uint8_t(reg));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(int(sTouchAddr), int(n)) != n) { while (Wire.available()) Wire.read(); return false; }
  for (size_t i=0;i<n;++i) out[i]=uint8_t(Wire.read());
  return true;
}
static bool sTouchWriteReg(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(sTouchAddr); Wire.write(uint8_t(reg>>8)); Wire.write(uint8_t(reg)); Wire.write(v);
  return Wire.endTransmission()==0;
}
static bool stickyTouchBegin(uint32_t i2cHz = 100000) {
  pinMode(42,OUTPUT); digitalWrite(42,HIGH); delay(300);
  if (!Wire.begin(3,2,i2cHz)) return false;
  Wire.setTimeOut(20);
  for (uint8_t a : {uint8_t(0x5D), uint8_t(0x14)}) {
    pinMode(41,OUTPUT); digitalWrite(41,LOW);
    pinMode(21,OUTPUT); digitalWrite(21, a==0x5D?LOW:HIGH);
    delay(20); digitalWrite(41,HIGH); delay(20);
    digitalWrite(21,LOW); delay(50); pinMode(21,INPUT); delay(30);
    sTouchAddr=a;
    uint8_t id[4]{};
    if (sTouchReadReg(0x8140,id,4) && id[0]=='9'&&id[1]=='1'&&id[2]=='1'&&id[3]==0)
      return sTouchWriteReg(0x814E,0);
  }
  sTouchAddr=0; return false;
}
struct StickyTouchPoint { uint8_t id; uint16_t x,y,size; };
// Returns contact count for a NEW frame (0 = new release), -1 = no new frame.
// Points are LOGICAL portrait coordinates (x 0..479 left->right, y 0..799
// top->bottom); raw y is flipped here so callers never handle the sign.
static int stickyTouchRead(StickyTouchPoint* out, int maxOut) {
  if (!sTouchAddr) return -1;
  uint8_t st=0; if (!sTouchReadReg(0x814E,&st,1)) return -1;
  if (!(st&0x80)) return -1;
  uint8_t count=st&15;
  if (count>5) { sTouchWriteReg(0x814E,0); return -1; }
  uint8_t raw[40]{};
  if (count && !sTouchReadReg(0x814F,raw,count*8)) return -1;
  for (uint8_t i=0;i<count && i<maxOut;++i) {
    const uint8_t* p=&raw[i*8];
    uint16_t ry = uint16_t(p[3]|(p[4]<<8));
    out[i]={p[0],uint16_t(p[1]|(p[2]<<8)),uint16_t(ry>=799?0:799-ry),uint16_t(p[5]|(p[6]<<8))};
  }
  sTouchWriteReg(0x814E,0);
  return count;
}

// ---------------- display (portrait-logical draw -> 90deg SSD layout) -------
static uint8_t* sFb=nullptr;
static spi_device_handle_t sSpi=nullptr;
static bool sDisplayReady=false;

static bool sWaitBusy(uint32_t timeoutMs=10000) {
  uint32_t start=millis();
  while (digitalRead(18)==HIGH) { if (uint32_t(millis()-start)>=timeoutMs) return false; delay(1); }
  return true;
}
static bool sSpiXfer(bool data, const uint8_t* b, size_t n) {
  if (!sSpi) return false;
  digitalWrite(8,HIGH); digitalWrite(16, data?HIGH:LOW);
  for (size_t off=0; off<n; off+=1024) {
    size_t chunk=(n-off)>1024?1024:(n-off);
    spi_transaction_t t={}; t.length=chunk*8; t.tx_buffer=b+off;
    if (spi_device_polling_transmit(sSpi,&t)!=ESP_OK) return false;
  }
  return true;
}
static bool sSpiCmd(uint8_t c){ return sSpiXfer(false,&c,1); }
static bool sReg8(uint8_t c,uint8_t v){ return sSpiCmd(c)&&sSpiXfer(true,&v,1); }
static bool stickyDisplayBegin() {
  pinMode(8,OUTPUT); digitalWrite(8,HIGH);
  pinMode(15,OUTPUT); digitalWrite(15,HIGH);
  pinMode(16,OUTPUT); digitalWrite(16,LOW);
  pinMode(17,OUTPUT); digitalWrite(17,HIGH);
  pinMode(18,INPUT);
  pinMode(47,OUTPUT); digitalWrite(47,HIGH);
  delay(100);
  spi_bus_config_t bus={}; bus.mosi_io_num=14; bus.miso_io_num=-1; bus.sclk_io_num=13;
  bus.quadwp_io_num=-1; bus.quadhd_io_num=-1; bus.max_transfer_sz=4096;
  if (spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK) return false;
  spi_device_interface_config_t dev={}; dev.clock_speed_hz=10000000; dev.mode=0; dev.spics_io_num=15; dev.queue_size=1;
  if (spi_bus_add_device(SPI2_HOST,&dev,&sSpi)!=ESP_OK) return false;
  sFb=(uint8_t*)heap_caps_malloc(SSD_FRAME, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if (!sFb) sFb=(uint8_t*)heap_caps_malloc(SSD_FRAME, MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  if (!sFb) return false;
  memset(sFb,0xFF,SSD_FRAME);
  digitalWrite(17,LOW); delay(10); digitalWrite(17,HIGH); delay(10);
  if (!sWaitBusy()) return false;
  if (!sSpiCmd(0x12)||!sWaitBusy()) return false;
  const uint8_t softstart[]={0xAE,0xC7,0xC3,0xC0,0x80};
  const uint8_t driverout[]={0xDF,0x01,0x02};
  const uint8_t xwin[]={0,0,0x1F,0x03};
  const uint8_t ywin[]={0,0,0xDF,0x01};
  const uint8_t zero[]={0,0};
  sDisplayReady = sReg8(0x18,0x80) && sReg8(0x3C,0x01) &&
    sSpiCmd(0x0C)&&sSpiXfer(true,softstart,sizeof(softstart)) &&
    sSpiCmd(0x01)&&sSpiXfer(true,driverout,sizeof(driverout)) && sReg8(0x11,0x03) &&
    sSpiCmd(0x44)&&sSpiXfer(true,xwin,sizeof(xwin)) &&
    sSpiCmd(0x45)&&sSpiXfer(true,ywin,sizeof(ywin)) &&
    sSpiCmd(0x4E)&&sSpiXfer(true,zero,sizeof(zero)) &&
    sSpiCmd(0x4F)&&sSpiXfer(true,zero,sizeof(zero)) && sWaitBusy();
  return sDisplayReady;
}
static bool stickyDisplayRefresh() {
  if (!sDisplayReady) return false;
  if (!(sSpiCmd(0x24)&&sSpiXfer(true,sFb,SSD_FRAME))) return false;
  if (!(sReg8(0x22,0xF7)&&sSpiCmd(0x20))) return false;
  return sWaitBusy();
}
static void stickyDisplayClear(){ if(sFb) memset(sFb,0xFF,SSD_FRAME); }
// Logical portrait pixel (x 0..479, y 0..799) -> SSD (X=y, Y=x).
static void stickyDrawPixel(int x,int y,bool black) {
  if(!sFb||x<0||x>=LW||y<0||y>=LH) return;
  int sx = y;                // SSD column
  int sy = x;                // SSD row
  int byteIdx = sy*SSD_STRIDE + (sx>>3);
  uint8_t m = 0x80 >> (sx&7);
  if(black) sFb[byteIdx]&=~m; else sFb[byteIdx]|=m;
}
static void stickyDrawDot(int cx,int cy,int r) {
  for(int y=cy-r;y<=cy+r;++y) for(int x=cx-r;x<=cx+r;++x)
    if((x-cx)*(x-cx)+(y-cy)*(y-cy)<=r*r) stickyDrawPixel(x,y,true);
}

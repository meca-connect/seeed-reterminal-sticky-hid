/*
 * s3_mirror — determine whether the panel needs horizontal mirroring.
 *
 * Fills the LEFT half of the framebuffer black, applies the vendor mirror_x
 * (stickyMirrorX), and refreshes. You tell us which side looks black:
 *   - "RIGHT"  -> mirror_x is correct; keep mirroring in the final app.
 *   - "LEFT"   -> panel needs NO mirror; drop mirroring.
 * One question, no touch, no BLE.
 */
#include "../_common/sticky_hw.h"

void setup() {
  pinMode(45,OUTPUT); digitalWrite(45,HIGH);
  pinMode(46,OUTPUT); digitalWrite(46,HIGH);
  Serial.begin(115200); delay(800);
  Serial.println("S3 MIRROR TEST — which half of the screen is BLACK?");
  Serial.printf("Display: %s\n", stickyDisplayBegin() ? "OK" : "FAILED");
  stickyDisplayClear();
  for (int y=0;y<D_H;++y)
    for (int x=0;x<D_W/2;++x) stickyDrawPixel(x,y,true); // black LEFT half of framebuffer
  stickyMirrorX();            // vendor mirror_x
  stickyDisplayRefresh();
  Serial.println("DRAWN black framebuffer-left-half WITH mirror. Is the black half on the LEFT or RIGHT of the screen?");
}

void loop() { delay(1000); }

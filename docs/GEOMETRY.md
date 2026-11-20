# Sticky display & touch geometry (verified)

Calibrated on the physical Sticky by touching visible targets. This is the
authoritative coordinate model for this device; earlier landscape assumptions
were wrong.

## Coordinate system (physical = logical)

Hold the Sticky with the USB-C port at the BOTTOM.

- The display is physically PORTRAIT: 480 pixels wide x 800 pixels tall.
- Logical origin is top-left. x runs 0..479 left->right; y runs 0..799 top->bottom.

(The part is sold as "800x480" because the SSD1677 controller's native frame
is 800x480 landscape; the panel is mounted rotated 90 degrees inside the card.)

## Touch -> logical (verified)

The Goodix GT911 reports 480 x 800. raw x is already left->right, but raw y
INCREASES TOWARD THE PHYSICAL TOP (raw y=0 is at the USB end) — verified by the
pointer test, where finger-up first moved the cursor down. The logical frame
flips that so y is top->bottom:

    logical_x = raw_x
    logical_y = 799 - raw_y

Touch read: address 0x5D, I2C0 SDA=3 SCL=2, INT=21, RST=41, power enable=42.
Sensor resolution register 0x8146 returns X=480, Y=800. Contact record at
0x814F: id, X lo/hi, Y lo/hi, size lo/hi, reserved. Ack by writing 0 to 0x814E
after reading the full record.

## Display logical -> SSD (verified)

The SSD1677 (800x480 native) is mounted so its 800 columns run top->bottom.
A logical pixel (x, y) is written at SSD column y and SSD row x:

    SSD_X = y
    SSD_Y = x

Framebuffer is 800x480 (100 bytes/row). Byte index = SSD_Y * 100 + SSD_X/8,
bit = 0x80 >> (SSD_X & 7). Write the buffer unmirrored.

SSD1677 pins: EN=47, RST=17, BUSY=18 (active HIGH), CS=15, DC=16, MOSI=14,
SCK=13; SD card CS=8 held HIGH. SPI2, mode 0, 10 MHz, 1 KiB transfers. Full
refresh only (write RAM BW 0x24, update 0xF7, master activate 0x20), ~1-2 s.

## Evidence chain

- s1_touch_blink: touch inverts whole panel -> touch reads AND refresh work.
- s2_touch_map: raw coords for free taps; ruled out no-rotation guesses.
- s2b_calibrate: guided 4-corner targets produced the raw table below.
- s3_mirror: a left-half fill appeared top/bottom -> panel is 90-degree mounted.
- s4_dot_follow: dot drawn at logical==raw lands under the finger -> model confirmed.

s2b corner table (framebuffer was landscape then; raw values are the ground truth):

    logical TL(120,100)  raw( 86,645)
    logical TR(680,100)  raw(127,115)
    logical BL(120,380)  raw(388,664)
    logical BR(680,380)  raw(372,122)

Top/bottom tracks raw X (86->388), left/right tracks raw Y inversely
(645->115). Together with the mirror test this yields the portrait model above.

## Implementation

`sketches/_common/sticky_hw.h` implements the verified model:

- `stickyTouchBegin()` / `stickyTouchRead()` return raw == logical portrait.
- `stickyDrawPixel(x,y,black)` applies the 90-degree transpose into the SSD
  framebuffer; `stickyDisplayRefresh()` sends it unmirrored.

Any app port (trackpad, etc.) must use 480x800 portrait logical coordinates,
not 800x480 landscape. The earlier one-shot `sticky_trackpad/` app assumed
landscape and is superseded pending a rebuild on this model.

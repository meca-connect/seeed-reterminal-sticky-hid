# Game plan

## Goal

Turn the reTerminal Sticky into a compact touch-driven HID device, taking the M5PaperS3 project as a design reference while preserving the Sticky's own board support, display, power behavior and touch semantics.

## Today's focus: establish the touch contract

Do not start gesture-to-HID work until raw touch behavior is understood. Begin with serial-only observation, no USB/BLE HID reports and no e-paper redraws.

1. Confirm exact device label/revision, firmware version, current USB identity and whether the shipped firmware/data must be preserved. Record the device's ID from its About screen. A connected USB serial dongle is not enough to positively identify the Sticky; identify it by unplug/replug or serial number before any upload.
2. Use the diagnostic probe to enable GPIO42 touch power, initialize the GT911 on I2C GPIO3/GPIO2 and record address/product ID/resolution. Try 0x5D and 0x14 only with a controller-correct reset/address sequence. Confirm reset/power timing from source and Seeed docs before changing pins.
3. Capture a baseline with no touch, then single taps in a 3x3 screen grid, short and long holds, slow/fast horizontal and vertical swipes, diagonals, edge-inward swipes and two-finger contacts. Record raw coordinates, contact count/IDs, INT transitions, sample interval, release behavior and dropped/duplicate events.
4. Establish coordinate range, origin, axes, rotation/mirroring, noise/jitter, maximum simultaneous contacts and latency from measured logs. Compare with 800x480 display geometry; do not infer behavior from the stock UI's gesture mappings.
5. Test whether contacts/INT continue while the e-paper app sleeps or its screen is in another state. Observe power behavior only through non-destructive logs; don't force sleep, reset or flash without explicit user go-ahead.
6. Summarize verified behavior, unresolved questions and the smallest practical interaction set: tap, drag, one-finger pointer, edge/vertical scroll, multi-touch gestures, wake behavior. Do not promise multi-touch until logs show contact IDs/counts over time.

### Today's data sheet

| Test | Procedure | Measurements / result |
|---|---|---|
| Device identity | About screen; USB unplug/replug | Pending |
| Controller probe | Power/reset, I2C address, product ID | Pending |
| Idle | 30 s untouched | Pending |
| Tap map | 3x3, 3 taps each | Pending |
| Hold | 0.2 s / 1 s / 3 s center | Pending |
| Swipe | Horizontal, vertical, diagonal; slow/fast | Pending |
| Edge gestures | Inward from each edge | Pending |
| Two-finger | Simultaneous contact; pinch/spread and parallel drag | Pending |
| Host collection | Serial record and timestamps | Pending |

## Later phases

### Phase 2 — driver and touch event model

Make a Sticky-specific GT911 driver/probe whose hardware setup is isolated from gesture logic. Track each contact's ID, coordinates, down/move/up transitions and timestamps. Keep raw device coordinates and transformed logical coordinates separately. Add bounded logging and a quiet mode so diagnostics don't perturb sampling.

### Phase 3 — display and interaction experiments

Verify display orientation and partial-refresh cost independently. Explore visible touch feedback with minimal, localized refreshes only after input measurements are done; e-paper latency/ghosting and touch sampling are distinct measurements.

### Phase 4 — HID transport and safety

Stock USB-C HID blocker confirmed from Seeed schematic revision 01, sheet 8: USB_DP/USB_DN route into CH343P USB-UART pins 7/8, not ESP32-S3 native USB. No native-USB bypass is shown on that sheet. A firmware-only TinyUSB change cannot turn this fixed USB-UART bridge into a HID endpoint. The MCU native-USB GPIO19/20 are assigned to the PDM microphone. Options: BLE HID for standalone input without modification; serial plus host companion for a wired prototype (not USB HID); or a separate native-USB MCU bridge/hardware modification, whose accessible wiring and electrical constraints must be verified first. OS-native multitouch touchpad reporting is separate from mapping multi-finger gestures to ordinary mouse/keyboard HID. Implement a disabled-at-boot, command-armed, bounded test mode with STOP, release reports, busy rejection and host-disable behavior; do not generate pointer/keyboard/scroll reports during ordinary boot or diagnostics.

### Phase 5 — gestures, UI, and power

Start with a small interaction set (pointer + tap/click, then vertical scroll). Separate the gesture engine from USB/BLE dispatch so both transports can be tested without rewriting touch logic. Measure idle power/touch wake behavior before adding sleep optimizations. Keep button mappings and e-paper UI simple due to the smaller 800x480 display.

### Phase 6 — verification and delivery

Build and test each saved sketch; report compilation, upload, touch-controller detection, captured raw contacts, host HID enumeration and user-observed gestures as separate evidence. Preserve an untouched factory image or document why backup is unavailable before replacing firmware. Keep source, exact board options, library versions, wiring/pin map, test logs and current limitations with the project. Do not push to GitHub unless asked.

## Stop gates

- No firmware upload until the exact Sticky is positively identified and the user agrees to replace its firmware; backup/preservation constraints must be addressed first.
- No display or power management changes during the touch-baseline phase.
- No HID movement or keystroke output during development unless the user explicitly arms a bounded test; warn before testing on a live host.
- No conclusions about multi-touch, latency, resolution mapping or sleep wake from docs alone; require observed data.

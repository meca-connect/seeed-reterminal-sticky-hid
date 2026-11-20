# Sticky PTP 4 — BLE Precision Touchpad research firmware

**Status: host-tested and Arduino-compiled only. Not flashed, paired, Windows-verified, latency-measured, or certified.** No hardware port was opened and no HID was sent during implementation. The existing GUI firmware/source is unchanged.

## Scope and reference

Standalone sketch: `sketches/s9_sticky_ptp_ble/s9_sticky_ptp_ble.ino`.
Primary target is Windows native four-contact PTP input mode 3. Windows interprets gestures and tap-to-click. PadType=1 (non-depressible glass), no physical-button emulation, replay clicks, synthetic motion, or fallback mouse. Mode 0 is read back faithfully but intentionally produces no new contact downs; switching out of mode 3 retires acknowledged contacts first. Other hosts are not promised full gestures.

Feature/report-map source was fetched using `gh api` at **1f29012bd593fc6ac6e2662db613e34b59b15545**:

- `meca-connect/m1-goodisplay-7.5-epd-trackpad/sketches/gdey075_ptp_ble/ptp_descriptor.h`
- Same directory, `gdey075_ptp_ble.ino`

Exact downloaded bytes are retained as `reference_*.txt` beside the sketch (not compiled). The descriptor and segmented 256-byte research/default HQA blob are adapted from that revision. The GitHub license endpoint returned HTTP 404; no upstream license was established. Preserve attribution and resolve permission before redistribution. The reference's working-Windows claims do **not** establish this port's Windows compatibility; the default HQA bytes are research status, not certification.

Reference fixes include lift-before-new-slot priority, transactional state commit on `notify()==true`, preserving observed up transitions across coalescing, validation, selective/latency readback, readiness/security/MTU gates, no firmware tap replay, initial negotiated interval, and removal of e-paper work and fixed maximum radio power.

## Hardware and geometry

Power hold GPIO45 HIGH, then lock GPIO46 HIGH. GT911 power GPIO42; SDA3/SCL2, INT21, RESET41. Same reset/address-selection/INT-sync delays as existing `_common/sticky_hw.h`, independently implemented without display includes. Probe 0x5D then 0x14; require product bytes `911\0`. Only GT911 write is ready acknowledgement at 0x814E. No configuration-memory writes.

Default bus **400 kHz**, per-transaction Wire timeout **5 ms**, nominal poll opportunity each 1 ms. Override `STICKY_PTP_I2C_HZ` / `STICKY_PTP_I2C_TIMEOUT_MS` with compiler defines (100 kHz is a diagnostic alternative). A bad/missing controller at boot remains not-ready; there is no automatic reset loop. Short I2C reads, ACK failures, invalid status/count, duplicate/out-of-range IDs, invalid coordinates and nonzero reserved byte reject the complete frame. A fifth contact rejects the whole frame rather than silently truncating it. No-ready preserves last-known input; no valid frame for >100 ms triggers safe release.

Verified project orientation: portrait, USB-C bottom; x=raw x 0..479, y=799-raw y 0..799. Reports double coordinates to logical **958 x 1598**; this does not increase actual sensor resolution.

Seeed's hardware overview gives 3.97-inch diagonal / 800x480 but no measured active-area dimensions:
https://www.seeedstudio.com/sticky/docs/en/device-guide/hardware-overview/

Approximate **user-configurable, not measured** default physical ranges are 204 x 340 in 0.01-inch units (51.816 x 86.36 mm), derived from the nominal diagonal and 480:800 aspect ratio, rounded to descriptor units. Override `STICKY_PTP_WIDTH_001IN` / `STICKY_PTP_HEIGHT_001IN` after measuring the actual active touch area. Do not use the chassis dimensions as the active area.

There is **no display initialization or refresh, including boot**. Existing e-paper ink remains. No Wi-Fi initialization, filesystem, UI, sensor extras, or hardware button actions.

## Reports, scheduling and state

- Four native tracking IDs, valid 0..15; no nearest-neighbor tracker. Packed contact=6 bytes, report=28 bytes (static assertions); 447-byte report map, independently parsed by host test.
- Features: caps=0x14; input mode 0/3; surface/button selective bits; latency 0/1; HQA segments 1..8, each segment index plus 32 bytes. Writes validate exact lengths/values; readback is canonical, even after invalid writes. Latency preference is recorded/read back but does not intentionally slow the low-latency implementation.
- Surface-disable forces acknowledged-contact releases even though new contact admission is disabled. Button selective bit is recorded but there are no button events to suppress.
- Single latest unsent sample plus a 16-bit pending-retirement mask. Missing acknowledged IDs are sticky until a release notification succeeds. Retire old contacts before admitting replacements, so four old/four new requires two paced reports. Reordered GT911 records do not reorder surviving host slots.
- A temporary contact that was never submitted may be coalesced away; this is unavoidable under latest-only sampling. An up transition for an acknowledged contact cannot be coalesced away, including up/down of the same ID between notifications.
- Build a report without changing acknowledged contact state; commit only if `notify()` returns true. False returns retain retirement state and retry no faster than one negotiated interval. No success-counter increment on a false return.
- Request connection `(min=6,max=6,latency=0,timeout=300)` (7.5 ms requested, 3 s supervision). Initialize pacing from `NimBLEConnInfo` immediately, and update when parameters change. The host may grant a slower interval or slave latency. One attempt per actual interval, timestamp from current time, never a catch-up loop.
- Resubmit current state continuously while held, even without a new ready frame, until stale timeout. No empty idle report stream.
- Atomic in-flight notification flag prevents more than one submitted notification awaiting NimBLE's local TX-status event. If that event never arrives, submission stays blocked until disconnect/reconnect rather than queueing old reports indefinitely. This behavior needs hardware validation. Local notify acceptance / TX status is not host application delivery or an end-to-end latency guarantee.
- Shared callback state/counters use ESP32 critical sections; the in-flight flag is atomic. Sensor, UART buffers and core state are owned only by the Arduino loop. Epoch snapshots before and after notify avoid committing work against a replaced connection/state; an event concurrent with actual radio submission cannot retract an already accepted notification.
- Connection, security, subscription, MTU readiness, protocol/suspend changes reset local contact/session state. Mode/selective changes clear latest input but preserve pending acknowledged lifts. New admissions are gated 400 ms after readiness or input-state changes; a new frame after the gate is required (no held pre-gate snapshot is replayed).

**ATT MTU must be >=31** for the complete 28-byte input value plus ATT overhead. Local preferred MTU is 64; this is not proof of negotiation. At MTU23/30 input is blocked, never truncated; startup/transition STATUS explicitly includes `mtu_block=1` and requirement. The host must perform a suitable MTU exchange. HQA feature values support normal long reads.

Bonding, encryption and subscription are required before report submission. LE Secure Connections bonding is requested with no-input/no-output Just Works (encrypted, but no MITM authentication). NimBLE's input and feature report attributes also require encrypted access. Prototype GAP name is **Sticky PTP 4**. PnP tuple is all-zero/unassigned source/vendor/product with prototype version 1, not an invented registered VID. That is intentionally not a production identity and may be a Windows interoperability issue; obtain legitimate assigned IDs before release.

## Latency and power tradeoffs

CPU is explicitly set to 240 MHz. `esp_bt_sleep_disable()` is called after NimBLE initialization, result recorded at startup. With `CONFIG_PM_ENABLE`, acquire `ESP_PM_CPU_FREQ_MAX` and `ESP_PM_NO_LIGHT_SLEEP` locks; with the installed core these features may be compiled out. No manual sleep or maximum TX-power override. BLE connection scheduling and GT911 scan rate remain physical/host constraints; requested interval and software polling are not measured end-to-end latency. A 1 ms loop yield allows the BLE task to run. Display work never blocks input.

UART0 via the stock CH343P at **115200**, exact FQBN below keeps native USB CDC disabled. No serial device was opened in this task. Commands are uppercase newline-terminated `PING` or `STATUS`; no movement/test/flash/reset commands. RX uses a 23-character bound and 16-byte per-loop budget; long lines are discarded. TX uses one fixed 1200-byte buffer and <=32 bytes per loop, only when UART has space; no unsolicited per-frame printing. One startup and bounded state-transition status may be emitted. STATUS requests received while a prior reply is pending are dropped.

STATUS includes connectivity/bond/encryption/subscription/readiness, mode/selective/latency/HQA, negotiated interval/slave latency/MTU, poll/frame/error totals, `notify_ok` (local accepted submissions), `notify_fail`, async status errors, epoch-discard count, feature R/W/invalid counters, software **sample-to-submit** mean/max/count. Timing starts after a complete valid I2C frame read, excludes sensor scan/bus-read time, includes repeated last-state reports/stale releases, and ends after notify returns; not finger-to-screen latency. Timing accumulation stops at 1,000,000 successful samples, counters saturate. `ready` is transport readiness and `enabled` additionally requires mode3/surface; the fixed 400ms gate is separate.

## Reproduce offline validation

From the project root:

```sh
python3 tests/test_ptp_run.py
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:FlashSize=32M,PSRAM=opi,CDCOnBoot=default,USBMode=hwcdc,FlashMode=dio \
  --build-path build/sticky-ptp-build \
  sketches/s9_sticky_ptp_ble
```

Installed/used Arduino CLI 1.5.1, ESP32 core 3.3.11, NimBLE-Arduino 2.5.1, g++ C++17. Five offline test executables passed with `-Wall -Wextra -Werror -fsanitize=address,undefined`; tests include actual production core/frame/features/link headers, descriptor parser and actual Wire adapter through existing fakes. No serial/BLE hardware interaction.

Actual vertical RED→GREEN evidence captured during implementation:

1. Core API stub built; `test_ptp_core.cpp:8` failed `c.sample(p,4,400000)` (exit134). Implement four-contact sampling/scaling/pacing; passed.
2. Added full replacement/failure/coalescing assertions; line20 failed `v.flags==1 && v.id<4` (exit134). Implement sticky retirement priority and successful-notify commit; passed.
3. Added malformed/stale/disable/reconnect/wrap slice; line37 failed `!c.sample(&bad,1,446000)` (exit134). Implement validation and stale release; passed. A preliminary misleading-indentation warning was fixed before the behavioral RED run.
4. Descriptor, protocol/features and readiness tests were introduced before their headers (missing-header compiler RED), then production headers implemented/adapted and tests passed. Supplementary stable-reorder/partial/full-lift and Wire-adapter tests also pass; those were additional coverage, not separate behavioral RED cycles.

Final compilation: **592143 bytes flash**, **36048 bytes static RAM**, **291632 bytes remaining RAM** using default 1310720-byte application partition. Build outputs retained under `sketches/s9_sticky_ptp_ble/artifacts/`; manifest includes SHA256 values. Build artifacts are compile evidence, not an instruction to flash.

## Remaining validation / safety gate

Before any later upload: positively reidentify the physical Sticky and current UART interface, confirm preservation/rollback for the currently flashed GUI and get explicit user permission. Do not infer identity from CH343P alone or use a stale tty path. No upload/reset/pairing occurred here.

Then separately validate GT911 identity, strict-frame assumptions (especially reserved/status bytes), four real fingers, no phantom reports after gate, actual security/MTU/interval, notification/TX callback behavior, feature negotiation and Windows Device Manager/PTP Settings recognition, individual/full/replacement lifts, reconnect and selective disable, host tap-to-click, four-finger gestures and real physical latency. Pair only after consent; compiled firmware automatically advertises and can send real-touch HID after a qualifying host connects. No Windows or physical behavior is claimed by host tests. Approximate physical dimensions, unassigned PnP identity, upstream license uncertainty and uncertified HQA remain prototype limitations.

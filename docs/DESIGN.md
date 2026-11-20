# Sticky Trackpad — MVP Design

Status: DRAFT v0.1 (2026-11-19) — design process kickoff.
Project decisions are logged in section 2; open questions in section 6.

## 1. Purpose

A portable, one-handed BLE trackpad built on the Seeed reTerminal Sticky
(ESP32-S3, 480x800 portrait e-paper + GT911 touch) that works as a normal
mouse on a Linux computer: responsive pointer, multi-finger scroll, and
on-screen scroll bars, with a clean dot-grid UI and minimal on-device
settings (connect / forget / orientation).

## 2. Locked scope decisions (2026-11-19)

- D1: GENERIC BLE HID MOUSE ("normal trackpad"). The Windows Precision
  Touchpad port (s9_sticky_ptp_ble) is PARKED — kept in tree, not developed.
- D2: The device must connect correctly to a Linux computer (BlueZ
  is the reference host) and stay usable across reboots (bonding).
- D3: Input rate must be high enough that the pointer "does not feel laggy"
  — this becomes a measured requirement (R4), not a vibe.
- D4: Mouse screen = simple, little/no text, grid of dots, cheap to refresh.
- D5: Settings = simple connect/forget GUI + orientation control.
- D6: BLE is the only transport (stock USB-C is wired to the CH343P UART;
  native USB pins go to the PDM mic — verified against schematic rev 01).

## 3. MVP requirements + acceptance criteria

R1 Responsive touch
   - GT911 frames read with intervals measured (min/avg/p95/max) while a
     finger moves continuously; no frame starvation, no stuck contacts.
   - No perceptible dead zones or coordinate jumps (ID-continuity tracked).

R2 Multi-touch scroll
   - Two-finger drag scrolls BOTH horizontally and vertically (
     Q1 answered 2026-11-19).
   - Direction polarity correct on screen; no axis crosstalk; scroll stops
     cleanly on lift (no runaway events).

R3 One-handed scroll bars
   - LEFT + RIGHT edge strips (Q2 answered 2026-11-19): single-finger
     drag inside either edge strip produces horizontal scroll, thumb-reachable
     without moving the hand.
   - Default polarity: drag toward top = scroll right, drag toward bottom =
     scroll left (tunable in P2).
   - Strips visible on the dot-grid screen (minimal ink, static draw).

R4 Fast input rate (measured, target numbers — adjust after P0 baseline)
   - Touch sampling: >= 100 Hz effective frame rate while moving.
   - BLE: negotiated connection interval <= 15 ms (target 7.5 ms),
     zero slave latency.
   - Motion reports to host: >= 100/s during continuous movement.
   - End-to-end (finger motion -> evdev event on the host): p95 <= 25 ms.
   - Evidence: timestamped capture rig (section 5); publish only sanitized
     aggregate results, never raw device logs.

R5 Simple mouse screen
   - Static dot grid, little or no text, drawn once per screen entry.
   - NO e-paper refresh in the touch/motion path (refresh only on discrete
     screen/state change). Partial refresh where the driver supports it.

R6 Settings GUI (connect / forget)
   - Shows connection state (icon/dots, not paragraphs).
   - FORGET host action on-device with a two-step confirm (touch-driven).
   - Re-pairing discoverable automatically when no bond/host present.

R7 Orientation
   - Manual setting, rotates UI + touch mapping in 0/90/180/270 steps
     (Q3 answered 2026-11-19). Persisted across reboots (NVS).

R8 Scope: MOUSE ONLY (Q4 answered 2026-11-19)
   - Numpad/keyboard screens dropped from MVP; maybe revisited post-MVP.
   - MVP screens: TRACKPAD (dot grid + edge strips) and SETTINGS
     (connect/forget/orientation).

## 4. Baseline (verified facts, do not re-litigate)

- Board: ESP32-S3 rev0.2, 8MB PSRAM, 32MB flash. Unique device identifiers
  and private backup details are intentionally not published.
- Touch: GT911 @0x5D, 1/2/3/4 simultaneous contacts confirmed; 5 unproven.
- Geometry (docs/GEOMETRY.md): logical 480x800 portrait, raw_x = logical_x,
  logical_y = 799 - raw_y; display SSD(X=y, Y=x).
- BLE HID chain proven end-to-end on a Linux host (s6): tap -> BlueZ uhid -> mouse.
- Buttons ACTIVE-LOW: POWER/OK=GPIO4 (short press only — PMIC powers off on
  long press), UP=GPIO5, DOWN=GPIO6.
- Historical firmware milestone: s8_sticky_gui advertised "Sticky Trackpad".
- s8 contains numpad/keyboard screens — MVP scope for those: Q4.

## 5. Measurement plan (phase P0, before new firmware features)

Device side (firmware telemetry via serial, bounded, out of motion path):
- STATS command: touch frames read, frame-interval histogram, reports sent,
  negotiated interval/latency/MTU, RSSI.

Host side (Linux):
- evdev timestamp capture on the uhid device during a scripted gesture
  matrix (continuous circles 10 s, edge-to-edge swipes, taps, 2F scrolls).
- Report-rate = events/s during motion; latency = serial TX marker vs evdev
  event time for taps/clicks (single-flight, clocks noted as approximate).

Deliverable: logs/rate-baseline-*.log + a short findings table in this doc.

## 6. Open questions — ALL ANSWERED 2026-11-19

- Q1: vertical scroll -> YES, both axes (R2).
- Q2: scroll bars -> LEFT + RIGHT edge strips (R3).
- Q3: orientation -> manual 0/90/180/270 rotate UI+touch, persisted (R7).
- Q4: numpad/keyboard -> dropped from MVP, maybe revisit later (R8).

## 7. Roadmap

- P0  Measure: flash a telemetry build over the CURRENT gesture baseline
      (s7 lineage, mouse-only), characterize rates/latency on the host.
- P1  MVP firmware (s12_mvp, compiled 2026-11-19): new sketch built on _common/sticky_hw.h +
      GestureEngine + mouse-only BleTransport; dot-grid screen, scroll
      bars, settings GUI, orientation. Host-tested logic where possible.
- P2  Usability pass: interactive tuning (gain, thresholds, scroll
      feel), reconnect robustness (bonding across reboots).
- P3  Post-MVP candidates (only if wanted): keyboard/numpad, PTP revival,
      battery/sleep behavior.

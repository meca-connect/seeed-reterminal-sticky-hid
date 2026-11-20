# Terminal multitouch viewer and Precision Touchpad feasibility

## Run the live viewer

```sh
python3 scripts/touch_visualizer.py --port /dev/ttyACM0
```

Rediscover the actual port first if the device was reconnected. Run in a normal terminal on the connected Linux host (SSH is fine). No Python packages required. Close Arduino Serial Monitor/other captures first: one reader only. Ctrl+C exits and releases the port. The viewer does not send serial commands, toggle reset lines, flash firmware, or produce HID input.

The grid labels contact positions by raw controller tracking ID, not by array index. The table shows X/Y and raw touch size (NOT pressure). Maximum displayed contact count is tracked. When data becomes stale, the grid is cleared and the last known contact count is explicitly labeled STALE, not falsely shown as a real release. Invalid/truncated frames, inconsistent contact counts and duplicate IDs are rejected rather than invented/repaired. Raw axes are shown with provisional 480x800 bounds; actual sensor bounds and physical orientation are not calibrated yet.

Optional capture (new filename, no overwrite):

```sh
python3 scripts/touch_visualizer.py --port /dev/ttyACM0 --log logs/my-live-test.log
```

Recorded real-hardware three-contact example (not a live display):

```sh
python3 scripts/touch_visualizer.py --replay logs/visualizer-live-03.log --snapshot-contacts 3
```

Five host tests pass: parser, rejection of malformed frames, grid/stale behavior, recorded replay CLI and pseudo-terminal streaming CLI. Fixtures are synthetic, explicitly separate from hardware evidence. Live physical UART reading was exercised and received actual touch frames and ALIVE heartbeats; the newer live log includes repeated three-contact frames and some four-contact frames. Raw four-contact reporting is not a controlled four-finger capacity qualification.

## What the viewer proves

It lets us inspect simultaneous positions/IDs, holds, motion, adding/removing fingers and mapping to raw coordinates. It does not prove palm rejection, true pressure, ID continuity across missing samples, physical latency, report rate qualification, host HID protocol acceptance or Microsoft certification.

## Precision Touchpad is a valid research target, not yet verified on Sticky

Microsoft specifies minimum three and maximum five concurrent contacts, plus mandatory fields including per-contact ID, Tip Switch, Confidence, X/Y, report contact count and 100us-unit scan time. Touchpad/configuration collections and feature-report negotiation must also be implemented; having three contacts alone is not sufficient.

Current diagnostic firmware polls every 20ms (configured maximum of 50 polls per second), logs text, and provides no PTP HID interface. Do not equate this diagnostic configuration to the controller's maximum scan rate or a compliant report pipeline. Microsoft's single-contact report-rate test specifies 125Hz; physical sensor capability, usable resolution, timing, accuracy and transport must be measured before claiming compliance. Do not synthesize extra samples to claim a higher sensor report rate.

Related RP2350 research showed native-USB TinyUSB three-contact report/feature infrastructure and Linux touchpad binding, but Windows MOVE was acknowledged without observed pointer movement. That research is NOT a proven Windows-working PTP implementation. Treat it as a protocol reference, not a verified transplant.

The Sticky stock USB-C routes to CH343P USB-UART, not ESP32 native USB. PTP is not impossible in general, but USB PTP via this connector cannot be achieved merely by enabling TinyUSB. A native-USB bridge or verified hardware reroute is one path; protocol documentation also discusses Windows HID Bluetooth miniports, but that does NOT establish this ESP32 BLE implementation's compatibility. A generic BLE mouse with locally recognized gestures must not be called a Windows Precision Touchpad.

## Sources checked

- Microsoft collection, contact count and required reports: https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-windows-precision-touchpad-collection
- Microsoft HID bus/protocol implementation: https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-protocol-implementation
- Single-contact report-rate test: https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-report-rate---single-contact
- Descriptor requirements: https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-required-hid-descriptors
- Prior project Windows diagnostic evidence: `WINDOWS-NO-MOVEMENT.md` in the separate RP2350 spike above.

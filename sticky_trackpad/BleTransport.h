#pragma once
// Adapted from the related M5PaperS3 trackpad project. BLE report map, pairing retry,
// single-host storage and interval adaptation retained. No USB transport here.
// Do not re-enable updateConnParams: it broke the earlier board/host pairing.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
static uint8_t BLE_HID_REPORT_MAP[] = {
    // ---- Mouse, Report ID 1 --------------------------------------------
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02,
    0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0,

    // ---- Keyboard, Report ID 2 -----------------------------------------
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x02,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x03,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x03,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0
};

class BleTrackpadTransport;

class BleTrackpadServerCallbacks final : public NimBLEServerCallbacks {
public:
    explicit BleTrackpadServerCallbacks(BleTrackpadTransport *owner) : owner_(owner) {}
    void onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) override;
    void onDisconnect(NimBLEServer *server, NimBLEConnInfo &connInfo, int reason) override;
    void onAuthenticationComplete(NimBLEConnInfo &connInfo) override;
    void onConnParamsUpdate(NimBLEConnInfo &connInfo) override;
private:
    BleTrackpadTransport *owner_;
};

class BleTrackpadTransport {
public:
    static constexpr uint8_t  MOUSE_REPORT_ID       = 1;
    static constexpr uint8_t  KEYBOARD_REPORT_ID    = 2;
    static constexpr uint8_t  MAX_BONDS             = 1; // exactly one saved host - reliability over capacity
    static constexpr uint32_t REPORT_INTERVAL_MS    = 8;     // fallback flush cadence until conn params are known
    static constexpr uint32_t PAIRING_WINDOW_MS     = 60000; // vestigial-safe: pairing is only entered at 0 bonds (indefinite)
    static constexpr uint32_t CONN_TUNE_DELAY_MS    = 1200;  // never back-to-back with security
    static constexpr uint16_t CONN_MIN_INTERVAL     = 6;     // v1.0: 7.5 ms advertising-side preference
    static constexpr uint16_t CONN_MAX_INTERVAL     = 9;     // v1.0: 11.25 ms advertising-side preference
    static constexpr uint16_t CONN_LATENCY          = 0;
    static constexpr uint16_t CONN_TIMEOUT          = 400;   // 4 s supervision timeout
    // Calling updateConnParams() at all - even delayed well after security -
    // reliably kills the link on this hardware/host combo (reason=520
    // Connection Supervision Timeout within seconds). Disabled; the v1.0
    // advertising-side preferred params are the only latency nudge.
    static constexpr bool     ENABLE_DELAYED_TUNING = false;

    // v1.0: LE Secure Connections retry state machine. Default is legacy
    // Just Works (proven). One SC retry per pairing window, then revert.
    enum class SecRetry : uint8_t { LEGACY = 0, SC_TRYING = 1, SC_EXHAUSTED = 2 };

    bool begin(const char *deviceName) {
        if (!NimBLEDevice::init(deviceName)) {
            Serial.println("[BLE] NimBLEDevice::init failed");
            return false;
        }

        // Legacy Just Works remains the default; SC is only switched on for
        // the single retry inside a pairing window (see maybeScRetry()).
        applyLegacySecurity();
        NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

        server_ = NimBLEDevice::createServer();
        if (!server_) {
            Serial.println("[BLE] createServer failed");
            return false;
        }

        callbacks_ = new BleTrackpadServerCallbacks(this);
        server_->setCallbacks(callbacks_, true);
        server_->advertiseOnDisconnect(true);

        hid_ = new NimBLEHIDDevice(server_);
        hid_->setManufacturer("Sticky Trackpad Research");
        hid_->setHidInfo(0x00, 0x03); // v1.0: remote-wake + normally-connectable
        hid_->setPnp(0x02, 0xFFFF, 0x5354, 0x0001); // research-only IDs, not a product allocation

        mouseInput_ = hid_->getInputReport(MOUSE_REPORT_ID);
        keyboardInput_ = hid_->getInputReport(KEYBOARD_REPORT_ID);
        keyboardOutput_ = hid_->getOutputReport(KEYBOARD_REPORT_ID);
        (void)keyboardOutput_;

        hid_->setReportMap(BLE_HID_REPORT_MAP, sizeof(BLE_HID_REPORT_MAP));
        hid_->setBatteryLevel(100);

        if (!server_->start()) {
            Serial.println("[BLE] server start failed");
            return false;
        }

        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        if (!adv) {
            Serial.println("[BLE] advertising object unavailable");
            return false;
        }
        adv->addServiceUUID("1812");
        adv->setAppearance(0x03C0);
        adv->enableScanResponse(true);
        adv->setName(deviceName);

        // Faster discovery and a low-latency connection preference. The host
        // is allowed to choose different parameters; this is only a request.
        adv->setMinInterval(32);  // 20 ms advertising interval
        adv->setMaxInterval(48);  // 30 ms advertising interval
        adv->setPreferredParams(CONN_MIN_INTERVAL, CONN_MAX_INTERVAL);

        // A virgin device must always be pairable. Once at least one host is
        // stored, pairing a *new* host becomes an explicit UI action.
        if (bondCount() == 0) {
            pairingMode_ = true;
            pairingDeadlineMs_ = 0; // no timeout until first host exists
            secRetry_ = SecRetry::LEGACY;
        }

        if (!adv->start()) {
            Serial.println("[BLE] advertising start failed");
            return false;
        }

        Serial.printf("[BLE] NimBLE %s advertising as \"%s\"; bonds=%d/%u\n",
                      NimBLEDevice::getVersion(), deviceName, bondCount(), MAX_BONDS);
        return true;
    }

    uint32_t reportsSent() const { return reportsSent_; }
    void releaseAll() {
        clearPendingMotion();
        mouseButtons_ = keyboardModifiers_ = 0;
        sendMouseReport(0,0,0,0);
        sendKeyboardReport();
    }
    bool isConnected() const { return connected_; }
    bool isReady() const { return connected_ && hidReady_; }
    bool isPairingMode() const { return pairingMode_; }
    uint32_t stateRevision() const { return stateRevision_; }

    int bondCount() const {
        int n = NimBLEDevice::getNumBonds();
        return n < 0 ? 0 : n;
    }

    std::string bondAddressString(int index) const {
        if (index < 0 || index >= bondCount()) return std::string();
        return NimBLEDevice::getBondedAddress(index).toString();
    }

    bool isBondConnected(int index) const {
        if (!isReady() || index < 0 || index >= bondCount()) return false;
        return NimBLEDevice::getBondedAddress(index) == currentPeerId_;
    }

    void mouseMove(int x, int y, int wheel, int pan) {
        pendingX_ += x;
        pendingY_ += y;
        pendingWheel_ += wheel;
        pendingPan_ += pan;
    }

    void mousePress(uint8_t buttonMask) {
        service(millis(), true);
        mouseButtons_ |= buttonMask;
        sendMouseReport(0, 0, 0, 0);
    }

    void mouseRelease(uint8_t buttonMask) {
        service(millis(), true);
        mouseButtons_ &= (uint8_t)~buttonMask;
        sendMouseReport(0, 0, 0, 0);
    }

    void mouseClick(uint8_t buttonMask) {
        mousePress(buttonMask);
        delay(12); // give the host an observable press before release
        mouseRelease(buttonMask);
    }

    void keyPress(uint8_t key) {
        if (key >= 0x80 && key <= 0x87) {
            keyboardModifiers_ |= (uint8_t)(1u << (key - 0x80));
            sendKeyboardReport();
        }
    }

    void keyRelease(uint8_t key) {
        if (key >= 0x80 && key <= 0x87) {
            keyboardModifiers_ &= (uint8_t)~(1u << (key - 0x80));
            sendKeyboardReport();
        }
    }

    // v1.0: optional modifier (e.g. KEY_LEFT_SHIFT) sent in the SAME report
    // as the key, so shifted characters arrive atomically. The modifier is
    // not persisted - the release report returns to keyboardModifiers_.
    void typeKey(uint8_t hidUsage, uint8_t modifier = 0) {
        if (!isReady() || !keyboardInput_) return;
        uint8_t mods = keyboardModifiers_;
        if (modifier >= 0x80 && modifier <= 0x87) {
            mods |= (uint8_t)(1u << (modifier - 0x80));
        }
        uint8_t report[8] = { mods, 0, hidUsage, 0, 0, 0, 0, 0 };
        keyboardInput_->setValue(report, sizeof(report));
        keyboardInput_->notify(report, sizeof(report));
        delay(12);
        sendKeyboardReport();
    }

    // Surface the real battery percentage through the standard BLE battery
    // service (hosts show it next to the device name).
    void updateBatteryLevel(uint8_t pct) {
        if (!hid_) return;
        hid_->setBatteryLevel(pct, isReady());
    }

    void goInactive() {
        pairingMode_ = false;
        pairingDeadlineMs_ = 0;
        hidReady_ = false;
        if (connected_ && server_ && connHandle_ != 0xFFFF) {
            server_->disconnect(connHandle_);
        }
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        if (adv) adv->stop();
        bumpState();
    }

    void goActive() {
        if (bondCount() == 0) {
            pairingMode_ = true;
            pairingDeadlineMs_ = 0;
            secRetry_ = SecRetry::LEGACY;
        }
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        if (adv) adv->start();
        bumpState();
    }

    void forgetAllAndPair() {
        hidReady_ = false;
        if (connected_ && server_ && connHandle_ != 0xFFFF) {
            server_->disconnect(connHandle_);
        }
        NimBLEDevice::deleteAllBonds();
        pairingMode_ = true;
        pairingDeadlineMs_ = 0; // zero bonds => pair indefinitely
        secRetry_ = SecRetry::LEGACY; // fresh window: SC retry budget restored
        applyLegacySecurity();
        NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
        if (adv) adv->start();
        Serial.println("[BLE] all bonds cleared; waiting for first pairing");
        bumpState();
    }

    void service(uint32_t now, bool force = false) {
        // Time out explicit add-a-device mode, but only when at least one bond
        // remains. A factory-new / fully-cleared unit stays discoverable.
        if (pairingMode_ && pairingDeadlineMs_ != 0 &&
            (int32_t)(now - pairingDeadlineMs_) >= 0) {
            pairingMode_ = false;
            pairingDeadlineMs_ = 0;
            Serial.println("[BLE] pairing window expired");
            bumpState();
        }

        // Kept for documentation; ENABLE_DELAYED_TUNING is false. See the
        // constant's comment for why updateConnParams must never be called.
        if (ENABLE_DELAYED_TUNING && isReady() && !connTuneRequested_ &&
            (now - authenticatedAtMs_) >= CONN_TUNE_DELAY_MS &&
            server_ && connHandle_ != 0xFFFF) {
            server_->updateConnParams(connHandle_, CONN_MIN_INTERVAL, CONN_MAX_INTERVAL,
                                      CONN_LATENCY, CONN_TIMEOUT);
            connTuneRequested_ = true;
            Serial.printf("[BLE] requested conn params %.2f-%.2f ms, timeout %.1f s\n",
                          CONN_MIN_INTERVAL * 1.25f, CONN_MAX_INTERVAL * 1.25f,
                          CONN_TIMEOUT * 0.010f);
        }

        // Do not emit HID notifications before encrypted/bonded state and CCCD
        // restoration are ready. This avoids losing the first movements on reconnect.
        if (!isReady()) { clearPendingMotion(); return; }
        if (!force && (now - lastMouseReportMs_) < reportIntervalMs_) return;
        if (!hasPendingMotion()) return;

        int8_t x = takeChunk(pendingX_);
        int8_t y = takeChunk(pendingY_);
        int8_t w = takeChunk(pendingWheel_);
        int8_t p = takeChunk(pendingPan_);
        sendMouseReport(x, y, w, p);
        lastMouseReportMs_ = now;
    }

    void handleConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
        (void)server;
        connected_ = true;
        hidReady_ = false;
        connHandle_ = connInfo.getConnHandle();
        currentPeerId_ = connInfo.getIdAddress();
        bondCountAtConnect_ = bondCount();
        clearPendingMotion();
        mouseButtons_ = 0;
        keyboardModifiers_ = 0;
        lastMouseReportMs_ = 0;
        authenticatedAtMs_ = 0;
        connTuneRequested_ = false;
        reportIntervalMs_ = REPORT_INTERVAL_MS; // until the real interval is known

        // Security first. Connection tuning is deliberately deferred (and
        // disabled - see ENABLE_DELAYED_TUNING).
        NimBLEDevice::startSecurity(connInfo.getConnHandle());
        Serial.printf("[BLE] connected handle=%u peer=%s bonds-before=%d pairing=%u sc=%u\n",
                      connInfo.getConnHandle(), currentPeerId_.toString().c_str(),
                      bondCountAtConnect_, pairingMode_, (uint8_t)secRetry_);
        bumpState();
    }

    void handleDisconnect(int reason) {
        bool wasPairingAttempt = pairingMode_ && authenticatedAtMs_ == 0;
        connected_ = false;
        hidReady_ = false;
        connHandle_ = 0xFFFF;
        mouseButtons_ = 0;
        keyboardModifiers_ = 0;
        clearPendingMotion();
        authenticatedAtMs_ = 0;
        connTuneRequested_ = false;

        Serial.printf("[BLE] disconnected, reason=%d\n", reason);

        // v1.0: a security-related drop mid-pairing (before auth completed)
        // feeds the same one-shot SC retry state machine as an explicit
        // authentication failure.
        if (wasPairingAttempt && isSecurityReason(reason)) {
            maybeScRetry("security-related disconnect");
        }
        bumpState();
    }

    void handleAuthentication(NimBLEConnInfo &connInfo) {
        currentPeerId_ = connInfo.getIdAddress();
        int after = bondCount();
        bool newBondCreated = after > bondCountAtConnect_;

        Serial.printf("[BLE] security: encrypted=%u authenticated=%u bonded=%u peer=%s bonds=%d new=%u\n",
                      connInfo.isEncrypted(), connInfo.isAuthenticated(), connInfo.isBonded(),
                      currentPeerId_.toString().c_str(), after, newBondCreated);

        if (!connInfo.isEncrypted() || !connInfo.isBonded()) {
            // v1.0: during a pairing attempt, try LE Secure Connections once
            // before giving up; after that revert to legacy and stop retrying.
            if (pairingMode_ && maybeScRetry("authentication failed")) {
                if (server_) server_->disconnect(connInfo.getConnHandle());
                return;
            }
            Serial.println("[BLE] security failed/not bonded -> disconnecting");
            if (server_) server_->disconnect(connInfo.getConnHandle());
            return;
        }

        if (pairingMode_) {
            // Pairing mode is only ever entered with zero bonds
            // (forget-and-pair). If a host we somehow still hold a bond for
            // reconnects here, it IS our one saved host - accept and close.
            if (after > MAX_BONDS) {
                Serial.printf("[BLE] bond store overflow (%d/%u) -> removing newest peer\n",
                              after, MAX_BONDS);
                NimBLEDevice::deleteBond(currentPeerId_);
                if (server_) server_->disconnect(connInfo.getConnHandle());
                return;
            }

            pairingMode_ = false;
            pairingDeadlineMs_ = 0;
            secRetry_ = SecRetry::LEGACY; // success: close out the retry state
            applyLegacySecurity();
            Serial.printf("[BLE] host stored (%d/%u)\n", after, MAX_BONDS);
        } else if (newBondCreated) {
            // New hosts are only accepted through explicit pairing mode.
            Serial.println("[BLE] unsolicited new bond -> deleting and disconnecting");
            NimBLEDevice::deleteBond(currentPeerId_);
            if (server_) server_->disconnect(connInfo.getConnHandle());
            bumpState();
            return;
        }

        hidReady_ = true;
        authenticatedAtMs_ = millis();
        connTuneRequested_ = false;
        bumpState();
    }

    void handleConnParamsUpdate(NimBLEConnInfo &connInfo) {
        // Adapt the notify flush cadence to the interval the host actually
        // granted. Clamp 4-15 ms: never finer than the gesture poll, never
        // coarser than ~one frame vs USB.
        float intervalMs = connInfo.getConnInterval() * 1.25f;
        uint32_t flushMs = (uint32_t)(intervalMs + 0.5f);
        if (flushMs < 4)  flushMs = 4;
        if (flushMs > 15) flushMs = 15;
        reportIntervalMs_ = flushMs;
        Serial.printf("[BLE] conn params now interval=%.2f ms latency=%u timeout=%.1f s -> flush every %lu ms\n",
                      intervalMs, connInfo.getConnLatency(),
                      connInfo.getConnTimeout() * 0.010f, (unsigned long)flushMs);
    }

private:
    friend class BleTrackpadServerCallbacks;

    void bumpState() { stateRevision_++; }

    void applyLegacySecurity() {
        NimBLEDevice::setSecurityAuth(true, false, false); // bonding, no MITM, legacy pairing
    }

    // Disconnect reasons (BLE_HS_ERR_HCI_BASE 0x200 + HCI code) that indicate
    // the link died over security rather than ordinary RF loss:
    //   517 = 0x205 authentication failure
    //   518 = 0x206 PIN or key missing
    //   523 = 0x20B connection terminated due to security (host side)
    //   525 = 0x20D connection terminated due to MIC failure
    //   531 = 0x213 remote user terminated (common when a host aborts pairing)
    static bool isSecurityReason(int reason) {
        switch (reason) {
            case 517: case 518: case 523: case 525: case 531: return true;
            default: return false;
        }
    }

    // One-shot LE Secure Connections retry. Returns true when a retry was
    // armed (caller should disconnect so the host re-attempts pairing under
    // the new security config). Never more than one SC attempt per window.
    bool maybeScRetry(const char *trigger) {
        if (secRetry_ == SecRetry::LEGACY) {
            secRetry_ = SecRetry::SC_TRYING;
            NimBLEDevice::setSecurityAuth(true, false, true); // LE Secure Connections on
            Serial.printf("[BLE] %s during pairing -> retrying ONCE with LE Secure Connections\n",
                          trigger);
            return true;
        }
        if (secRetry_ == SecRetry::SC_TRYING) {
            secRetry_ = SecRetry::SC_EXHAUSTED;
            applyLegacySecurity();
            Serial.printf("[BLE] %s again after SC retry -> reverting to legacy Just Works, no further retries this window\n",
                          trigger);
            return false;
        }
        Serial.printf("[BLE] %s; SC retry already exhausted this pairing window\n", trigger);
        return false;
    }

    static int8_t takeChunk(int &value) {
        int chunk = value;
        if (chunk > 127) chunk = 127;
        if (chunk < -127) chunk = -127;
        value -= chunk;
        return (int8_t)chunk;
    }

    bool hasPendingMotion() const {
        return pendingX_ || pendingY_ || pendingWheel_ || pendingPan_;
    }

    void clearPendingMotion() {
        pendingX_ = pendingY_ = pendingWheel_ = pendingPan_ = 0;
    }

    void sendMouseReport(int8_t x, int8_t y, int8_t wheel, int8_t pan) {
        if (!isReady() || !mouseInput_) return;
        uint8_t report[5] = { mouseButtons_, (uint8_t)x, (uint8_t)y, (uint8_t)wheel, (uint8_t)pan };
        mouseInput_->setValue(report, sizeof(report));
        if (mouseInput_->notify(report, sizeof(report))) ++reportsSent_;
    }

    void sendKeyboardReport() {
        if (!isReady() || !keyboardInput_) return;
        uint8_t report[8] = { keyboardModifiers_, 0, 0, 0, 0, 0, 0, 0 };
        keyboardInput_->setValue(report, sizeof(report));
        keyboardInput_->notify(report, sizeof(report));
    }

    NimBLEServer *server_ = nullptr;
    NimBLEHIDDevice *hid_ = nullptr;
    NimBLECharacteristic *mouseInput_ = nullptr;
    NimBLECharacteristic *keyboardInput_ = nullptr;
    NimBLECharacteristic *keyboardOutput_ = nullptr;
    BleTrackpadServerCallbacks *callbacks_ = nullptr;

    uint32_t reportsSent_ = 0;
    volatile bool connected_ = false;
    volatile bool hidReady_ = false;
    volatile bool pairingMode_ = false;
    volatile uint32_t stateRevision_ = 0;
    uint16_t connHandle_ = 0xFFFF;
    NimBLEAddress currentPeerId_;
    int bondCountAtConnect_ = 0;
    uint32_t pairingDeadlineMs_ = 0;
    uint32_t authenticatedAtMs_ = 0;
    bool connTuneRequested_ = false;
    SecRetry secRetry_ = SecRetry::LEGACY;

    uint8_t mouseButtons_ = 0;
    uint8_t keyboardModifiers_ = 0;
    int pendingX_ = 0, pendingY_ = 0, pendingWheel_ = 0, pendingPan_ = 0;
    uint32_t lastMouseReportMs_ = 0;
    uint32_t reportIntervalMs_ = REPORT_INTERVAL_MS; // adapted per-connection
};

void BleTrackpadServerCallbacks::onConnect(NimBLEServer *server, NimBLEConnInfo &connInfo) {
    owner_->handleConnect(server, connInfo);
}
void BleTrackpadServerCallbacks::onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) {
    owner_->handleDisconnect(reason);
}
void BleTrackpadServerCallbacks::onAuthenticationComplete(NimBLEConnInfo &connInfo) {
    owner_->handleAuthentication(connInfo);
}
void BleTrackpadServerCallbacks::onConnParamsUpdate(NimBLEConnInfo &connInfo) {
    owner_->handleConnParamsUpdate(connInfo);
}

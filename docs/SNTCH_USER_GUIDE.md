# SNTCH User Guide — ESP32-WROOM-32D

SNTCH is an offline-first classroom activity, attendance, and examination system. The ESP32-WROOM-32D creates the local Wi-Fi network, serves the SNTCH API, and stores supported classroom state in LittleFS. Teacher and student Android apps connect directly to it. Normal class use requires no internet, cloud, router, Raspberry Pi, or PC server. The Arduino Uno is not required and has no implemented serial link to SNTCH.

[Open the visual offline guide](SNTCH_VISUAL_USER_GUIDE.html) for diagrams, workflows, hardware maps, and troubleshooting.

## System map

```text
Teacher Android ─┐
                 ├── local Wi-Fi ── ESP32-WROOM-32D ── LittleFS
Student Android ─┘                 HTTP API + sessions   local state
```

## Hardware

Core operation needs a confirmed ESP32-WROOM-32D development carrier with USB/UART, a suitable USB cable/power source, and Android teacher/student devices. The exact carrier board has not been identified. Confirm its module marking, header pin labels, and USB port before flashing or wiring.

An optional **separate** TFT/button smoke test uses an ST7735 SPI display:

| Signal | GPIO |
|---|---:|
| TFT SCK / MOSI | 18 / 23 |
| TFT CS / DC / reset | 22 / 21 / 17 |
| Select / Back / Left / Right | 32 / 33 / 25 / 26 |

Buttons connect each GPIO to GND and use `INPUT_PULLUP`. This test is not part of the classroom firmware. ESP32 signals are 3.3 V; check the exact TFT breakout’s logic, VCC, and backlight requirements. GPIO6–11 are flash connections; GPIO12 is a boot-strapping pin.

## First-time firmware setup

From the repository root:

```bash
cd firmware
pio run -e esp32wroom32d
```

For a **confirmed new installation only**, create and upload the filesystem image before the firmware:

```bash
pio run -e esp32wroom32d -t buildfs
pio run -e esp32wroom32d -t uploadfs
pio run -e esp32wroom32d -t upload
pio device monitor -b 115200
```

`uploadfs` replaces the LittleFS partition, including saved classroom data. It is not a routine repair command. The image contains a pending first-use marker. At first boot, SNTCH creates the initial v1 state and installed marker. Firmware mounts without automatic formatting; missing or invalid state on an initialized installation stops the service. Preserve data and serial logs before recovery.

Check serial logs for LittleFS mounted, classroom state loaded/initialized, access point up, HTTP API listening, and `ready`. A storage error prevents normal service startup.

## Connect Android

1. In Android Wi-Fi settings, join **`SNTCH-EXAM`**, password **`sntch-1234`**.
2. Stay on the network if Android reports “no internet”; that is expected.
3. Open **`http://192.168.4.1/api/health`**. A valid JSON response with `"ok":true`, the expected SSID/IP, and `"storageError":false` shows the health route answered over HTTP.
4. In the app, use controller address `http://192.168.4.1` and tap **Test**.

Wi-Fi visibility, phone association, HTTP health, and complete app workflows are separate checks. Health does not prove teacher login, student joining, or submissions.

## Teacher workflow

1. Open SNTCH, choose **Teacher**, check the controller address, and tap **Test**.
2. Sign in with the firmware’s configured teacher PIN. Current source default: `246810`; change it before a real class.
3. **Students:** add roster lines as `number, name, section`. Re-importing a number updates that student.
4. **Activities** or **Examinations:** create a draft and publish it.
5. **Sessions:** create and start a session for a published item; share its code with students. Session details show participation and submissions and include pause/resume/end actions.
6. Use **Attendance** to mark present/late/absent by section and date. The phone provides the date.
7. Use **Results** to review activity responses and exam results. Answer review is controlled by the configured results policy.

## Student workflow

1. Join `SNTCH-EXAM`; open SNTCH and choose **Student**.
2. Check the session code, enter it with the student number, and join.
3. Open available work from Home. Activity drafts stay on the phone until submitted.
4. In examinations, answers autosave as they change. Review before submitting; timeout or session end may auto-submit.
5. Check results when available. Rejoin after reconnect if asked; rejoining the same open session is idempotent.

## Local storage and restart

Classroom state, submissions, attempts, attendance, and supported session data are stored on controller LittleFS. The app also keeps activity/exam drafts locally on the phone. A live session returns paused after restart; exam work is restored from the last saved checkpoint. Teacher/student tokens are RAM-only and may need to be renewed.

The pending marker in an explicitly uploaded fresh filesystem authorizes first state creation. Once `sntch_initialized.txt` exists, a missing `/state.json` is treated as data loss, not a new class. Valid legacy v1 state can be adopted without changing its format. Malformed or unsupported state is left in place and startup stops. Do not use `uploadfs`, formatting, or full-chip erase to troubleshoot a storage error.

## Troubleshooting

| Problem | Safe check | Next action |
|---|---|---|
| Wi-Fi absent | Check USB power and serial AP logs. | Confirm WROOM carrier and use `esp32wroom32d`; do not erase flash. |
| Phone disconnects | Stay joined to the no-internet network; check password and distance. | Reconnect manually in Android settings. |
| Health endpoint fails | Confirm phone Wi-Fi, `192.168.4.1`, and “HTTP API listening” log. | Save logs and restart once; do not upload a filesystem image. |
| App cannot reach controller | Set address to `http://192.168.4.1`; tap Test. | Keep Android on SNTCH Wi-Fi and read the app error. |
| Teacher/student action rejected | Check PIN, roster number, session code/state, and section target. | Ask teacher to verify session status. |
| State will not load | Record serial error and preserve flash contents. | Seek data-safe recovery; never format as the first diagnostic. |
| TFT blank/buttons fail | This is the separate peripheral test; check wiring, GPIO map, GND, and display voltage. | Verify the breakout requirements before reconnecting power. |
| Upload fails | Confirm carrier, serial port, cable, and WROOM environment. | Retry normal app upload; avoid full-chip erase. |

## Verification limits

PlatformIO builds, native core tests, host HTTP/restart tests, and Android JVM tests verify software and simulated workflows. They do not verify a physical ESP32, Wi-Fi range, TFT wiring, Android UI on a device, or a full phone-to-controller class. Physical setup and classroom workflows still need testing on the actual board.

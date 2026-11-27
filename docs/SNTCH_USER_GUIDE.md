# SNTCH User Guide — Option C

SNTCH is an offline-first classroom activity, attendance, and examination system. In Option C, an ESP-32U development board runs the local Wi-Fi network and classroom service, drives a TFT display, and stores classroom records on an external SPI microSD module. Teachers and students use the Android app. The TFT starts automatically and shows the connection or activity QR; there is no UNO or panel-button menu.

[Open the visual offline guide](SNTCH_VISUAL_USER_GUIDE.html) for diagrams, workflows, hardware maps, and troubleshooting.

## System map

```text
Teacher Android ─┐
                 ├── local Wi-Fi ── ESP-32U ── SPI microSD (FAT32)
Student Android ─┘                     └───── TFT (QR/status)
```

## Hardware

Use the [Option C wiring diagram](../hardware/wiring.html#phone-admin) and [pin table](../firmware/phone_admin/README.md#wire-the-board). You need the USB-powered ESP-32U development board, a compatible ST7735 SPI TFT, an external SPI microSD module with 3.3 V logic, and a FAT32 card. Verify the actual header labels and the TFT and microSD supply requirements before connecting power. Follow the board-specific antenna guidance in the setup guide. Options A and B are covered in the [alternate builds guide](DUAL_HARDWARE.md).

## First-time firmware setup

For a new installation, [prepare the FAT32 card](../firmware/phone_admin/README.md#prepare-the-card-and-flash): create `/sntch` on the card and copy [`sntch_pending_init.txt`](../firmware/data/sntch_pending_init.txt) into it. Insert the card while the controller is powered off. Keep existing cards and data intact when updating.

Set `SNTCH_AP_PASSWORD` and `SNTCH_TEACHER_PIN` as described in [firmware configuration](FIRMWARE.md#3-configuration). From the repository root:

```bash
pio run -d firmware
pio run -d firmware -t upload
pio device monitor -d firmware -b 115200
```

The default target is `esp32_phone_admin`. No `uploadfs` step is needed. Check the serial log for the microSD mount, classroom state load, and controller readiness. If the TFT says **MICROSD UNAVAILABLE**, the classroom service cannot start.

## Connect Android

1. In Android Wi-Fi settings, join **`SNTCH-EXAM`** using the password set when the firmware was built.
2. Stay on the network if Android reports “no internet”; that is expected.
3. Open **`http://192.168.4.1/api/health`**. A valid JSON response with `"ok":true`, the expected SSID/IP, and `"storageError":false` shows the health route answered over HTTP.
4. In the app, enter the address on the TFT (default `http://192.168.4.1`) and tap **Test**. The app on this branch uses manual entry; QR scanning requires a compatible app build.

Wi-Fi visibility, phone association, HTTP health, and complete app workflows are separate checks. Health does not prove teacher login, student joining, or submissions.

## Teacher workflow

1. Open SNTCH, choose **Teacher**, check the controller address, and tap **Test**.
2. Sign in with the private teacher PIN configured when the firmware was built.
3. **Students:** add roster lines as `number, name, section`. Re-importing a number updates that student.
4. **Activities** or **Examinations:** create a draft and publish it.
5. **Sessions:** create and start a session for a published item; share its code with students. Session details show participation and submissions and include pause/resume/end actions. A ready, ended or expired session can be deleted; the phone saves a backup first and only then removes it from the controller.
6. Use **Attendance** to mark present/late/absent by section and date. The phone provides the date.
7. Use **Results** to review activity responses and exam results. Answer review is controlled by the configured results policy.

## Student workflow

1. Join `SNTCH-EXAM`; open SNTCH and choose **Student**.
2. Enter the teacher’s session code and your student number, then join. With exactly one live activity, the TFT also shows that activity’s join code; use the teacher’s code for exams or multiple live activities.
3. Open available work from Home. Activity drafts stay on the phone until submitted.
4. In examinations, answers autosave as they change. Review before submitting; timeout or session end may auto-submit.
5. Check results when available. Rejoin after reconnect if asked; rejoining the same open session is idempotent.

## Local storage and restart

Classroom state, submissions, attempts, attendance, and supported session data are stored under `/sntch` on the microSD card. The app keeps drafts and deletion backups on the phone. A live session returns paused after restart; exam work is restored from the last saved checkpoint. Teacher/student tokens are RAM-only and may need to be renewed.

Deleting a session is a two-step handover. The controller first writes a copy of the session to `/del/<id>.json` and marks it *pending deletion*, which hides it from students. The teacher app saves that copy on the phone under `files/session-backups/` and only then confirms the deletion, which removes the controller copy. If the phone cannot save the backup it does not confirm, so the session stays pending with its server-side copy intact; pending deletions survive a restart, and the phone keeps its copy after confirmation as the teacher’s record.

On a new card, the pending marker authorizes first state creation. A missing state on an initialized card or a malformed state file stops startup. Option C cannot fall back to internal flash. If storage fails, save the serial log, power off before removing the card, and back up the card before attempting recovery.

## Troubleshooting

| Problem | Safe check | Next action |
|---|---|---|
| Wi-Fi absent | Check USB power, TFT storage status, and serial logs. | If microSD is unavailable, power off and check the module wiring and card first. |
| Phone disconnects | Stay joined to the no-internet network; check password and distance. | Reconnect manually in Android settings. |
| Health endpoint fails | Confirm phone Wi-Fi, `192.168.4.1`, and “HTTP API listening” log. | Save logs and restart once; do not upload a filesystem image. |
| App cannot reach controller | Set address to `http://192.168.4.1`; tap Test. | Keep Android on SNTCH Wi-Fi and read the app error. |
| Teacher/student action rejected | Check PIN, roster number, session code/state, and section target. | Ask teacher to verify session status. |
| Deleted session stays pending | The teacher phone has not completed the handover; the controller still holds the session copy in `/del`. | Open the teacher app on the SNTCH network and let it finish or cancel that pending deletion; never erase flash to clear it. |
| State will not load | Record the serial error and preserve the card contents. | Back up the card before attempting recovery. |
| TFT blank | Check TFT power, backlight, GND and the Option C pin map. | Confirm the breakout’s voltage and ST7735 init settings. |
| Upload fails | Confirm board, serial port, data cable and `esp32_phone_admin` target. | Close the serial monitor before retrying the upload. |

## Verification limits

PlatformIO builds, native core tests, host HTTP/restart tests, and Android JVM tests verify software and simulated workflows. They do not verify a physical ESP32, Wi-Fi range, TFT wiring, Android UI on a device, or a full phone-to-controller class. Physical setup and classroom workflows still need testing on the actual board.

# Option C · ESP32 phone administration

`esp32_phone_admin` boots into the SNTCH classroom controller as soon as USB power is applied. The TFT shows the local Wi-Fi name, manual address, and connection QR once the microSD card and controller are ready. The Android app handles teacher sign-in, sessions, attendance, and records. No UNO or panel buttons are needed.

## Wire the board

Follow the [Option C wiring diagram](../../hardware/wiring.html#phone-admin) and match the GPIO labels on the board.

| Connection | ESP32 pin |
| --- | --- |
| TFT and microSD SCK | GPIO18 |
| TFT and microSD MOSI (SD DI) | GPIO23 |
| microSD MISO (SD DO) | GPIO19 |
| microSD CS | GPIO27 |
| TFT CS | GPIO22 |
| TFT D/C | GPIO21 |
| TFT RESET | GPIO17 |

Use common ground and confirmed 3.3 V logic. A separate level shifter is unnecessary when both modules already use 3.3 V logic. Check the modules’ supply and backlight requirements and the ESP32 board’s regulator capacity before drawing peripheral power from its 3.3 V pin. The SD module must release MISO when deselected. Attach the compatible external antenna to the ESP-32U and leave USB unplugged while wiring.

## Prepare the card and flash

1. Use a blank FAT32 card. On the computer, create a folder named `sntch` in the card’s root. Copy [`sntch_pending_init.txt`](../data/sntch_pending_init.txt) into that folder as its only file: `/sntch/sntch_pending_init.txt`. This authorizes first-boot initialization. Safely eject the card and insert it with the ESP32 off.
2. Set `SNTCH_AP_PASSWORD` (private WPA2 password, at least eight characters) and `SNTCH_TEACHER_PIN` (private PIN, at least eight digits) in your build environment.
3. Confirm the board has at least 4 MB of flash. From the repository root, use PlatformIO:

   ```sh
   pio run -d firmware -e esp32_phone_admin
   pio run -d firmware -e esp32_phone_admin -t upload
   ```

   Option C uses the card directly; no `uploadfs` step is needed. Flashing this target does not migrate records from the other builds’ LittleFS partition.

## Use and verify

1. Power up. Join the controller’s Wi-Fi from your phone and open the SNTCH app. Scan **Controller QR** or enter the displayed address (default `http://192.168.4.1`). Use the teacher PIN to sign in. Wi-Fi credentials and the PIN are set at build time.
2. Start an activity in the app. With exactly one live activity, the TFT shows its student join QR. When that activity pauses or ends, the TFT shows the controller QR again. Use the app’s join codes for exams or when multiple activities are live. The manual address still works for teacher connection while the TFT shows a join QR.
3. Create a test session or record, restart, and verify that it persists. The controller does not format the card. If the display reports **MICROSD UNAVAILABLE**, power off, check the card, then restart. Serial diagnostics at 115200 baud give more detail. There is no LittleFS fallback, and the card should not be removed while powered. The firmware can recover an interrupted file replacement, but it cannot repair a damaged FAT32 filesystem.

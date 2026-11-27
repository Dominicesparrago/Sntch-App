# SNTCH hardware wiring and bench build

Open [the interactive wiring blueprint](../hardware/wiring.html) in a browser. The [supplied TFT preview](../hardware/SNTCH_Hardware_Faces_UI_Preview.html) is preserved unchanged; the blueprint embeds a separate simulation. `hardware/config.json` is the source for the firmware pin constants, wiring diagram and simulated hold timings. After changing it or the reference artwork, run `node tools/generate_panel.mjs`; review the netlist and rerun checks before building.

## Option A alternative: Version A dual-controller

The Option A build keeps the **UNO R3** responsible for the four physical buttons, TFT rendering,
menus, expressions/animations and QR display/actions. The **ESP-32U development board** provides
Wi-Fi, the HTTP/API service, classroom/session state and QR data. The existing framed UART2 protocol
is unchanged. Option C (`esp32_phone_admin`) is the default PlatformIO target; this guide covers
the available Option A build. Version B remains another alternate.

The supplied photos show a 38-pin ESP-32U board with micro-USB, EN and BOOT buttons, an onboard
USB-UART bridge and 3.3 V regulator, and an IPEX/U.FL-style antenna socket. The IC markings are
unreadable, so the USB-UART model, regulator current rating and flash capacity are not confirmed.
GPIO16/RX2, GPIO17/TX2, 3V3, GND and 5V are visible at the headers. Match pin names printed on the
actual board; the diagram is not a physical scale template.

### Version A GPIO and connection reference

| From | Through / to | Purpose |
|---|---|---|
| UNO D13, D11, D10, D9, D8 | U1 5 V input → 3.3 V output → TFT SCK, MOSI, CS, D/C, RESET | UNO renders the TFT; do not bypass U1 |
| UNO A1/TX | U1 → ESP header GPIO16/RX2 | UNO-initiated UART2 commands |
| ESP header GPIO17/TX2 | U2 3.3 V input → 5 V output → UNO A0/RX | UART2 responses |
| ESP board 3V3 | U1 VCC and U2 VCCA; TFT VCC only if current/supply limits are verified | 3.3 V logic; never join UNO 5 V |
| ESP GND, UNO GND, U1/U2 GND, TFT GND | Common ground | Required signal reference |
| ESP board micro-USB | USB 5 V/data source | Board power and onboard programming; separate from UNO USB-B |
| ESP board U.FL/IPEX socket | Compatible 2.4 GHz, 50 Ω antenna | RF only—not GPIO or power |

UNO buttons remain on D2–D5. A0/A1 are UART pins; D0/D1 stay free for UNO USB. GPIO16/17 UART2
remain separate from the board’s USB programming UART0. No TFT or buttons connect to the ESP32 in
the selected profile.

### Version A bill of materials

| Status | Component | Quantity / notes |
|---|---|---|
| Required | UNO R3; pictured ESP-32U 38-pin development board | 1 each |
| Required | Matching 2.4 GHz Wi-Fi antenna | 1; confirm exact U.FL/IPEX connector series before buying |
| Required | UNO USB-B cable and ESP32 micro-USB data cable / USB 5 V source | 1 each; power boards through separate USB ports |
| Required | 1.8-inch SPI TFT, 4 normally-open buttons, jumpers, half-size and mini breadboards | Verify TFT controller, header, VCC and backlight requirements |
| Required | U1 SN74LVC244A; U2 SN74LVC1T45 | 1 each; alternatives below |
| Required | 100 nF bypass capacitors | 3: one at U1 and one at each U2 supply pair |
| Conditional | SOIC/TSSOP breadboard adapters | Only for the purchased package; e.g. SOIC-20 and SOIC-14 for the found LVC245/HCT125D parts |
| Conditional | Regulated 3.3 V peripheral supply | Only if the board’s 3V3 output cannot power U1/TFT safely. Never parallel it with the board’s 3V3 output or back-power the ESP32. |
| Eliminated | Bare-module carrier/boot network, external EN/BOOT buttons, external USB-UART adapter, separate ESP32 3.3 V regulator | Integrated on the pictured development board; verify onboard ratings before loading its 3V3 output |

### Translator directions and alternatives

- **UNO 5 V → ESP32/TFT 3.3 V (six channels):** SN74LVC244A at 3.3 V, with 5 V-tolerant inputs confirmed for the exact suffix. Alternatives: one 74LVC245A (set A→B and enable outputs), one 74LVC541A (enable both active-low enables), or **two** 74LVC125A (four channels each). Define unused inputs and check the purchased manufacturer’s datasheet/package pinout.
- **ESP32 3.3 V TX → UNO 5 V RX (one channel):** SN74LVC1T45 with VCCA=3.3 V, VCCB=5 V and A→B direction. Alternatives: one 74HCT125, 74AHCT125 or 74HCT245 powered at 5 V; HCT/AHCT input thresholds accept 3.3 V high, unlike plain HC.
- **The found 74LVC245 + 74HCT125D pair can serve as one part in each direction** if the exact manufacturer part numbers/datasheets confirm LVC 5 V input tolerance at 3.3 V and HCT 3.3 V input-high compatibility at 5 V. Never connect UNO 5 V directly to an ESP32 GPIO.

## Version B: integrated ESP32-only alternate

Open [B · ESP32-only](../hardware/wiring.html#esp32-only) for the alternate GPIO map and [exact breadboard holes](../hardware/wiring.html#breadboard-placement). The ESP32 runs the existing offline classroom AP/HTTP server, TFT, four buttons, Controller QR and Activity QR. It does not use the UNO, U1 or U2.

Connect the verified 3.3 V TFT and four buttons to the pictured development board’s labeled GPIO header. Power the board through micro-USB and attach a compatible external 2.4 GHz antenna before Wi-Fi operation. Use the 3V3 output for the TFT only after checking regulator headroom; otherwise use a separate peripheral supply without tying its output to the board 3V3 pin.

| Version B device | ESP32 GPIO assignments |
|---|---|
| TFT | GPIO18 SCK, GPIO23 MOSI, GPIO22 CS, GPIO21 D/C, GPIO17 RESET; GND and a verified 3.3 V feed |
| Buttons | GPIO25 UP, GPIO26 DOWN, GPIO33 MENU, GPIO32 SELECT; switch to GND with `INPUT_PULLUP` |

All four buttons use **one half-size breadboard**, not the mini. These are the default placements for pin pairs spaced two columns apart (5.08 mm on a standard breadboard):

| Switch | GPIO jumper | Normally open contacts | Return to GND rail |
|---|---|---|---|
| UP | GPIO25 → A4 | E4 and E6 | A6 → −6 |
| DOWN | GPIO26 → A10 | E10 and E12 | A12 → −12 |
| MENU | GPIO33 → A16 | E16 and E18 | A18 → −18 |
| SELECT | GPIO32 → A21 | E21 and E23 | A23 → −22 |

The [placement diagram](../hardware/wiring.html#breadboard-placement) also offers **adjacent columns** (2.54 mm) and **across the center gap**; the exact contact and return holes update when selected. Use the option that fits without bending pins. Verify the input and return are open at rest and connect only when pressed, including after inserting a four-pin switch.

**Power:** ESP32 GND goes to half-size rail hole `−2`, the same `−` rail as the four switch returns. Count power rail holes separately from terminal columns. `+19/−20` feed the TFT only if the board 3V3 output has verified headroom and the TFT accepts 3.3 V. Use `+22` for a separate TFT LED supply **only** if its pin is explicitly rated for direct 3.3 V with onboard current limiting. Do not connect an LED supply to a GPIO.

### Build and bring up Version B

```sh
pio run -d firmware -e esp32_solo
pio run -d firmware -e esp32_solo -t upload --upload-port YOUR_ESP_USB_OR_UART0_PORT
```

For a **confirmed blank board only**, initialize LittleFS with `pio run -d firmware -e esp32_solo -t uploadfs --upload-port YOUR_ESP_USB_OR_UART0_PORT`. It replaces the filesystem; do not run it when classroom data must be kept. Set the AP password and teacher PIN before classroom use (see [firmware configuration](FIRMWARE.md#configuration)). The configured AP limit is ten connected phones/devices total, including a teacher's device. It is not the 50-student roster limit.

**Storage:** No microSD adapter is wired or initialized. The TFT's SPI is write-only, with no MISO connection. **Menu → Storage** reports LittleFS in internal ESP32 flash.

**Do not use the dual firmware on Version B.** Its GPIO17 is UART TX, not TFT reset. The older [ESP32 TFT/buttons demo](../firmware/esp32_wroom32d_tft_buttons/) remains a separate pin bench check. On Version B, **MENU + SELECT (2.5 s)** refreshes an offer or the displayed live activity code; **UP + DOWN (3 s)** toggles display standby while the AP, HTTP API and exam clocks continue running. Neither profile physically disconnects power.

### Breadboard precautions (both versions)

- **Half-size power rails:** use regulated 3.3 V and common GND. Keep the UNO's 5 V supply off these rails. Bridge corresponding halves only if continuity measurements show they are split and the wiring needs both halves. Never bridge 3.3 V to GND.
- **Mini breadboard, Version A only:** MENU and SELECT use the mini. Run a jumper from half-size **GND** to the shaded lower strip. The two switch returns use two other holes in that same five-hole strip, not the upper bank across the trench. Do not run a positive supply to the mini.
- The diagrams show typical hole layouts, not verified part footprints or the ESP32 carrier's physical header order. Use contacts that are open at rest and closed when pressed. If no pin placement fits unforced, use a compatible adapter or two short leads to the listed holes. The development board is powered through micro-USB; do not wire its 5V input from the UNO.

## Version A: development-board implementation notes

Use the selected dual-controller view in the [interactive wiring blueprint](../hardware/wiring.html) and the GPIO mapping above. The old bare-module wiring does not apply.

### Parts and electrical limits

- **UNO R3:** Use a proper USB-B supply. A0 receives and A1 transmits at 9600 8N1; D0/D1 stay free for USB. UP, DOWN, MENU and SELECT use D2–D5 with `INPUT_PULLUP`. Wire each normally open pushbutton to ground using opposite contacts. Check continuity; four-leg tact switches have internally common contact pairs.
- **ESP-32U development board:** Power and flash it through micro-USB. The photos show onboard EN/BOOT buttons, a USB-UART bridge and a 3.3 V regulator. Their exact IC markings/current ratings and the board flash size are unreadable. GPIO16/RX2 and GPIO17/TX2 are visibly exposed as UART2. Do not connect a separate supply to the board 3V3 pin while USB is connected.
- **External antenna:** Connect a 2.4 GHz, 50 Ω Wi-Fi antenna with a plug that exactly mates with the board’s IPEX/U.FL-style socket before Wi-Fi use. The precise connector series cannot be confirmed from the photos. Do not force the connector or pull its coax cable.
- **Boot/programming:** The external carrier boot network, EN/RESET circuit and USB-UART adapter are unnecessary for the photographed development board: its EN/BOOT buttons and USB-UART interface are onboard. Use the buttons if automatic bootloader entry fails.
- **3.3 V logic translation:** U1 is [SN74LVC244A](https://www.ti.com/lit/ds/symlink/sn74lvc244a.pdf), at 3.3 V with both active-low enables low. Confirm the exact suffix tolerates 5 V inputs at 3.3 V. Route UNO D13/SCK, D11/MOSI, D10/CS, D9/D/C, D8/RESET and A1/TX through six channels. Define unused inputs and leave unused outputs open. U2 is [SN74LVC1T45](https://www.ti.com/lit/ds/symlink/sn74lvc1t45.pdf): VCCA=3.3 V, VCCB=UNO 5 V, DIR=A→B; GPIO17/TX2 to A, B to UNO A0/RX. Fit three 100 nF bypass capacitors total, one at U1 and one at each U2 supply.
- **1.8 TFT SPI 128x160 v1.1:** Build profile is write-only SPI **ST7735**, `INITR_BLACKTAB`, rotation 1 (160×128 landscape). The marking does not verify the actual header order, voltage rating, init tab or backlight circuit. The diagram shows a conditional 3.3 V VCC feed, not a universal breakout connection. SPI pin aliases such as `SCL`/`SDA` are only valid if the board identifies them as SPI SCK/MOSI. No MISO, SD card or touch wires are used. If LED power is not built in, implement the part's specified current-limited circuit. Enable `-DSNTCH_PANEL_BACKLIGHT=1` on the UNO only after verifying an active-high 3.3 V **logic enable** input and wiring D6 through U1. A pin marked `LED` may be a power connection, not a logic enable. Never source a high-current backlight from D6.

**There is no physical power latch in this build.** UP+DOWN saves a classroom checkpoint, then sleeps the TFT and disables the optional backlight. The UNO continues polling keys; the ESP32 remains powered and its AP and API stay available. If true off is required later, design a separately powered wake circuit (momentary button or always-on combo detector), latching controller, and load switch rated for both rails’ inrush and peak current. Include reverse/backfeed prevention on USB and data lines and a shutdown-acknowledge path before cutting the rails. A fully unpowered UNO cannot monitor either button.

### Build and upload Version A

From the repository root:

```sh
pio run -d firmware -e uno_display -e esp32_dual
pio run -d firmware -e uno_display -t upload --upload-port YOUR_UNO_PORT
pio run -d firmware -e esp32_dual -t upload --upload-port YOUR_ESP_USB_PORT
```

For a **confirmed blank board only**, initialize LittleFS using `pio run -d firmware -e esp32_dual -t uploadfs --upload-port YOUR_ESP_USB_PORT`. This replaces the filesystem; do not run it when classroom data must survive. The existing 4 MB partition layout is retained, but the photos do not reveal this board’s flash size; verify it before using `uploadfs` or changing partitions. Configure private SoftAP credentials before building (see [firmware configuration](FIRMWARE.md#configuration)).

## Controls and session behavior

- On the face, UP/DOWN cycle all 12 expressions; MENU or SELECT opens the original nine-item menu. MENU returns or cancels, SELECT confirms, and UP/DOWN navigate. Individual short/long events are classified on release (25 ms debounce, 700 ms long press). Only a short SELECT confirms a destructive menu item. Exact two-key gestures take priority, suppress individual actions even on cancellation, and latch until all keys are released.
- **MENU + SELECT (2.5 s):** On the Activity QR screen, refresh the displayed live activity's join code. The ESP32 checks the displayed generation against the current session and commits the new code to LittleFS before acknowledging. The old code stops admitting new joins; already authenticated students, roster, submissions and the session's internal ID are preserved. Without exactly one live activity, the Activity QR shows a clear error rather than choosing a class silently. Use the teacher app to create, start or resume the session. Students can enter the code shown on the panel.
- Elsewhere the same hold opens/refreshes the **Controller QR**. This contains the real local HTTP address and a rotating offer ID, not a credential. Join the device AP before scanning. The app uses the address and verifies the server independently; the teacher still signs in. Refresh does not revoke existing teacher logins or disable manual pairing to the same address. If a scan fails, use the controller address (default `http://192.168.4.1`), or enter the displayed activity code manually.
- **UP + DOWN (3 s):** Save and enter software standby, or wake after releasing and starting a fresh hold. The menu Shutdown entry uses the same checkpoint. On save failure or timeout, the UNO stays awake and shows an error. Menu Restart checkpoints and reboots the ESP32; the existing backend restores live sessions as paused until the teacher resumes them.
- Status paints actual AP, UART, teacher-login, client, session, submission and LittleFS values where available. Help Requests remains in the menu but has no current backend feed; Needs Attention and Teacher Responding are manually viewable rather than fabricated. No battery or unmeasured network metrics are reported. The UNO uses the same extracted sprite, palette, bitmap font, face shapes and captions; it approximates the 340 ms browser feature morph with a scanline reveal and uses discrete gaze/blink frames and a 300 ms slide. The browser retains the original full rendering engine.

## Framed UART v1

UART2 uses a UNO-initiated stop-and-wait protocol. Frames are bounded by `7e`; each literal `7e` or `7d` in the body is escaped as `7d` followed by the byte XOR `20`. The body is `[version:1, type:1, sequence:2, length:1, payload:0…32, CRC:2]`. Integers are little-endian; CRC-16/CCITT-FALSE covers version through the end of payload. The parser drops malformed or incomplete frames and resynchronizes at the next delimiter; the partial-frame gap limit is 80 ms.

| Request | Payload after link cookie | Response |
|---|---|---|
| `01 Hello` | UNO boot counter instead of cookie | `81 HelloAck`: new/current cookie, echoed boot counter |
| `02 Status` | — | `82 Snapshot`: flags, AP clients, open sessions, live joined, FS used %, submissions (u16), prior-client-loss flag, firmware (7 bytes), reserved zero byte, SSID (12 bytes) |
| `03 Button` | event `1…4` short, `5…8` long (UP, DOWN, MENU, SELECT) | `83 Ack`: requested type |
| `04 GetQr` / `05 RestartQr` | kind `0` controller / `1` activity; activity restart also supplies displayed generation (u32) | `84 QrInfo`: kind, generation u32, side, byte count u16, matrix CRC u16, backend session ID u32, label (8 bytes) |
| `06 ReadQr` | generation u32, offset u16 | `85 QrBits`: generation, offset, up to 22 bytes |
| `07 Standby` / `08 Wake` / `09 Reboot` | — | `83 Ack`: requested type |

Except for the Hello request, each payload starts with the ESP32-assigned 32-bit link cookie; terminal errors use `86 Error` plus a one-byte reason. The ESP32 caches the last response so retries with the same sequence and body do not repeat a mutation. The UNO retries up to three attempts total, 1 s apart; UNO EEPROM bytes 0–3 identify its boot epoch. On a stale epoch or exhausted timeout it renegotiates and **does not automatically replay** an uncertain restart or checkpoint. The wire link assumes trusted physical access; CRC and the cookie protect framing and reset recovery, not against deliberate serial injection. Network updates use a periodic status request rather than unsolicited bytes to protect the software UART receive window.

The ESP32 uses the vendored MIT-licensed [Nayuki QR generator](https://github.com/nayuki/QR-Code-generator/tree/v1.8.0/c). Symbols are capped at v5 (37 modules); no RGB images cross UART. Bits are row-major, MSB first, with unused final bits zero. The UNO checks dimensions, offsets, generation, total length and whole-matrix CRC before showing a code. The 90×90 slot preserves a four-module white quiet zone and at least 2 TFT pixels per module. Oversize data produces a manual-entry fallback, not a scaled-down invalid code.

## Validation and bench acceptance

```sh
bash tools/test_dual_panel.sh
bash tools/native_build.sh
```

The UNO build reports about 1.1 KB static SRAM (of 2 KB); the 320-byte RGB565 scanline and 172-byte packed QR buffer are included. This report is not a physical stack-watermark measurement. Native tests use a fake UART, clock and SPI sink, and render all 12 faces, nine menu selections and the transferred QR matrices with the actual UNO drawing code.

**Unperformed physical acceptance:** With power removed, check rail isolation and grounds, then measure both supply and logic levels before plugging in either controller or the TFT. Verify UART idle levels and crossed TX/RX on a scope, then send status and QR traffic while moving through menus. Test early release, third-button rejection, repeated holds without releasing, standby/wake, AP loss, UNO reset with ESP32 running, and ESP32 reset with UNO running. Check TFT colors, rotation, offsets, backlight, physical QR scan range and refresh with actual phones. On an active classroom session, verify that the previous join code fails, logged-in students retain work, and a reboot restores the saved session. Do not treat the HTML validator or browser simulation as proof of wiring or connectivity.

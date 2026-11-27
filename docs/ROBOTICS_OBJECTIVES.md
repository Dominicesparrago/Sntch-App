# SNTCH Robotics Documentation — Option C Objectives

## General Objective

To design, develop, and evaluate SNTCH, an ESP32-based classroom
controller that allows teachers and students to manage attendance, activities,
and examinations through an Android app over a local Wi-Fi network without
internet access.

## Specific Objectives

1. **Build the Option C controller.** Connect the TFT and external SPI
   microSD module to one ESP-32U. The TFT starts automatically and shows the
   connection details or activity QR; the phone app provides the controls.
2. **Integrate the SPI peripherals.** Share clock and MOSI between the TFT
   and card module, give each its own chip-select signal, and verify the
   ESP32 can read the card on MISO.
3. **Provide offline classroom connectivity.** Configure the ESP-32U as a Wi-Fi
   access point and verify that teacher and student Android devices can connect
   to the controller while internet access is unavailable.
4. **Support teacher and student workflows.** Enable teachers to manage class
   rosters, record attendance, publish activities and exams, and review
   submissions. Enable students to join a session, submit their work, and view
   results when the teacher makes them available.
5. **Preserve classroom records.** Store attendance, submissions, and session
   data on the external FAT32 microSD card. Verify that saved
   records are restored after a controller restart and that a previously live
   session returns paused.
6. **Evaluate the complete system.** Test automatic TFT startup and card
   access, then run a classroom demonstration in which a teacher
   starts a session, a student joins and submits work, and the teacher reviews
   the submission. Record the results and any limitations observed.

## Supporting Documentation

- [Option C hardware wiring](../hardware/wiring.html#phone-admin)
- [Option C card setup and flashing](../firmware/phone_admin/README.md)
- [System architecture](ARCHITECTURE.md)
- [Testing and verification](TESTING.md)

# SNTCH Android app

One APK contains both roles. The role gate remembers the choice; switching roles does not erase
data on the controller.

## 1. Toolchain (verified here)

| Item | Value |
|---|---|
| Gradle | 9.5.0 (wrapper committed) |
| Android Gradle Plugin | 9.3.1 — **built-in Kotlin**; do not apply `org.jetbrains.kotlin.android` |
| Kotlin / Compose compiler | 2.2.10 |
| compileSdk / targetSdk | 36 |
| minSdk | 26 |
| UI | Jetpack Compose, Material 3, Forest Tech theme |
| HTTP | `HttpURLConnection` + `org.json` (no Retrofit/OkHttp dependency) |
| Tests | JUnit 4 + real `org.json` on the JVM classpath |

`JAVA_HOME` on this machine points at a missing `jdk-21`. A working JDK is
`C:\Program Files\Java\jdk-25`; export it before Gradle:

```bash
export JAVA_HOME="C:/Program Files/Java/jdk-25"
```

## 2. Build and install

```bash
cd android
./gradlew :app:testDebugUnitTest     # 33 JVM tests
./gradlew :app:assembleDebug         # app/build/outputs/apk/debug/app-debug.apk
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The app needs no API key, no Play Services and no internet permission beyond the LAN socket.
Cleartext HTTP is allowed in `network_security_config.xml` because the controller serves plain
HTTP on a private access point.

The user must connect the phone to `SNTCH-CLASS` in system settings; the app does not request
location or Wi-Fi control permissions and does not silently switch networks.

## 3. Code map

```
data/
  SntchApi.kt        transport, error envelope → ApiException, unreachable → NetworkException
  Models.kt          wire models + parsing (tolerant of absent optional fields)
  Repositories.kt    LocalStore (SharedPreferences), TeacherRepository, StudentRepository,
                     probeController (health check)
ui/
  Theme.kt           Forest Tech colors/typography from the HTML prototype
  Components.kt      cards, badges, buttons, fields, dialogs, status tones, date/clock helpers
  TeacherApp.kt      controller + drawer shell + dashboard, sessions, roster, attendance,
                     activities, exam builder, results/review, settings, PIN sign-in
  StudentApp.kt      join, home, activity response, timed exam, results, profile
  Root.kt            AppContainer, role gate
```

Presentation never calls the network directly: screens call a controller, the controller calls a
repository, the repository calls `SntchApi`.

## 4. Workflows

**Teacher**

1. Connect to the network, open the app, choose Teacher, test the address, sign in with the PIN.
2. Students → add/update the roster as `number, name, section` lines.
3. Activities or Examinations → create a draft, publish it.
4. Sessions → New session → choose the published item and duration → start.
5. Session detail shows the code, join instructions, live participants, submissions and scores;
   pause/resume/end are here.
6. Attendance → pick section and date → mark present/late/absent or mark all present.
7. Results → open a submission to read the response or answer sheet and mark activities reviewed.
8. Device & settings → address, show-correct policy, device diagnostics, sign out.

**Student**

1. Connect to `SNTCH-CLASS`, choose Student, enter session code and student number.
2. Home shows the session state and open work.
3. Activities: write 10–500 characters and submit; the draft stays on the phone until submitted.
4. Examinations: start when the session is live; answers autosave after each change; pause freezes
   the clock; review before submitting; timeout auto-submits.
5. Results: exam score, pass/fail, and answer review when the teacher allows it.

## 5. Connection and offline behaviour

- Teacher state and student state poll every 3 s while the relevant screen is active.
- A failed poll shows a "Reconnecting…" banner but keeps the last snapshot on screen; changes are
  blocked while the controller is unreachable.
- Activity text and exam answers are written to `SharedPreferences` on each edit. On reconnect or
  app restart the draft is restored and merged over the controller's autosaved answers.
- A 401 clears the stored token: the teacher returns to the PIN screen, the student returns to
  Join. Re-joining the same open session is idempotent.
- Submission is idempotent on the controller, so retrying after a lost response is safe.

## 6. UI/design mapping (prototype → app)

| Prototype element | Android implementation |
|---|---|
| Deep forest sidebar with grouped nav | `ModalNavigationDrawer` with gradient drawer and grouped destinations |
| Light workspace, white cards, hairline borders | `SntchCard`, Forest color tokens |
| Emerald gradient primary buttons | `PrimaryButton` with horizontal emerald gradient |
| Status pills (Present/Late/…, Live/Paused/…) | `Badge` + `statusTone` (colour plus text, never colour alone) |
| Segmented attendance control | `SelectChip` row per student |
| Exam screen with timer, progress, review | `ExamTaking` composable with server-owned clock |
| Student bottom nav | Material 3 `NavigationBar` |
| Dashboard stat tiles and device card | `StatTile`, controller card with storage bar |
| QR scan | removed — no camera, no fake scanner |

Typography uses the platform sans family with the prototype's weights and a monospace face for
codes, times and scores. The prototype's Google fonts are not bundled to keep the app fully
offline.

## 7. Not implemented in the app

- QR scanning (no camera permission; codes are typed)
- Automatic Wi-Fi joining
- CSV import/export UI
- Teacher directory / multiple teachers
- Dark-mode-specific badge palette (dark theme itself follows the system)
- Deleting students, activities or submissions (no destructive endpoints in V1)
- Two attempts per examination

## 8. Verification scope

The app has been compiled and its data layer exercised against a real loopback HTTP server in JVM
tests. It has **not** been installed on a phone or emulator here, so no UI rendering, navigation
gesture or on-device timing claim is made. `docs/TESTING.md` lists the manual checks to run on a
device.

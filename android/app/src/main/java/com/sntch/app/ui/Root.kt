package com.sntch.app.ui

import android.content.Context
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import com.sntch.app.data.DeviceInfo
import com.sntch.app.data.Prefs
import com.sntch.app.data.SntchApi
import com.sntch.app.data.StudentRepository
import com.sntch.app.data.TeacherRepository
import com.sntch.app.data.describeFailure
import com.sntch.app.data.probeController
import kotlinx.coroutines.launch

/**
 * App-wide dependencies. Repositories share one Prefs instance so switching
 * roles or addresses behaves consistently.
 */
class AppContainer(context: Context) {
    val prefs = Prefs(context)
    private fun api() = SntchApi(prefs.baseUrl)
    val teacher = TeacherRepository({ api() }, prefs)
    val student = StudentRepository({ api() }, prefs)
}

@Composable
fun SntchRoot(container: AppContainer) {
    var role by remember { mutableStateOf(container.prefs.role) }

    Surface(Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
        when (role) {
            "teacher" -> TeacherApp(container.teacher, container.prefs) {
                role = null
                container.prefs.role = null
            }
            "student" -> StudentApp(container.student, container.prefs) {
                role = null
                container.prefs.role = null
            }
            else -> RoleGate(container) { picked ->
                role = picked
                container.prefs.role = picked
            }
        }
    }
}

@Composable
private fun RoleGate(container: AppContainer, onPick: (String) -> Unit) {
    val scope = rememberCoroutineScope()
    var address by remember { mutableStateOf(container.prefs.baseUrl) }
    var probe by remember { mutableStateOf<DeviceInfo?>(null) }
    var probing by remember { mutableStateOf(false) }
    var probeError by remember { mutableStateOf<String?>(null) }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(20.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Spacer(Modifier.height(24.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .size(48.dp)
                    .clip(RoundedCornerShape(14.dp))
                    .background(Forest.primaryButton),
                contentAlignment = Alignment.Center,
            ) { Text("S", color = Color(0xFF052E16), fontWeight = FontWeight.Black) }
            Spacer(Modifier.width(12.dp))
            Column {
                Text("SNTCH", style = MaterialTheme.typography.headlineLarge)
                Text(
                    "Offline-first classroom system",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        Text(
            "The controller is an ESP32-WROOM-32D that creates its own Wi-Fi network. Connect this phone to " +
                "SNTCH-EXAM, then choose how you are using the app.",
            style = MaterialTheme.typography.bodyLarge,
        )

        SntchCard(title = "Controller") {
            Field(address, { address = it }, "Address", keyboardType = KeyboardType.Uri)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                QuietButton(if (probing) "Checking…" else "Test", onClick = {
                    probing = true
                    probeError = null
                    container.prefs.baseUrl = address
                    scope.launch {
                        try {
                            probe = probeController { SntchApi(address.trim()) }
                        } catch (failure: Throwable) {
                            probe = null
                            probeError = describeFailure(failure)
                        } finally {
                            probing = false
                        }
                    }
                })
                QuietButton("Save", onClick = {
                    container.prefs.baseUrl = address
                    probeError = null
                    probe = null
                })
            }
            val found = probe
            if (found != null) {
                InfoBanner(
                    "${found.name.ifEmpty { "Controller" }} · ${found.firmware} · ${found.students} students",
                    Tone.OK,
                )
            }
            val failed = probeError
            if (failed != null) InfoBanner(failed, Tone.BAD)
            Text(
                "Default: http://192.168.4.1 (the controller's address on its own network).",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        RoleCard(
            kicker = "TEACHER",
            title = "Run the classroom",
            body = "Import the roster, take attendance, publish activities and examinations, " +
                "open sessions, and review results. Requires the teacher PIN.",
            action = "Open teacher console",
        ) {
            container.prefs.baseUrl = address
            onPick("teacher")
        }

        RoleCard(
            kicker = "STUDENT",
            title = "Join and answer",
            body = "Join with the session code, answer activities, take timed examinations, " +
                "and see your results. Work stays on the controller.",
            action = "Start as student",
        ) {
            container.prefs.baseUrl = address
            onPick("student")
        }

        Text(
            "No account, no internet, no cloud. Every record lives on the classroom controller.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun RoleCard(
    kicker: String,
    title: String,
    body: String,
    action: String,
    onSelect: () -> Unit,
) {
    Surface(
        shape = RoundedCornerShape(16.dp),
        color = MaterialTheme.colorScheme.surface,
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outline),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Column(Modifier.padding(16.dp)) {
            Text(
                kicker,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.primary,
            )
            Text(title, style = MaterialTheme.typography.titleLarge)
            Spacer(Modifier.height(4.dp))
            Text(
                body,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(12.dp))
            PrimaryButton(action, onClick = onSelect)
        }
    }
}

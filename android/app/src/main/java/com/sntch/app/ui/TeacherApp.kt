package com.sntch.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DrawerValue
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalDrawerSheet
import androidx.compose.material3.ModalNavigationDrawer
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
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
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sntch.app.data.ApiException
import com.sntch.app.data.AttendanceDay
import com.sntch.app.data.DeviceInfo
import com.sntch.app.data.Item
import com.sntch.app.data.NewQuestion
import com.sntch.app.data.LocalStore
import com.sntch.app.data.Prefs
import com.sntch.app.data.Session
import com.sntch.app.data.Submission
import com.sntch.app.data.SubmissionDetail
import com.sntch.app.data.TeacherRepository
import com.sntch.app.data.TeacherState
import com.sntch.app.data.describeFailure
import com.sntch.app.data.probeController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

sealed interface TeacherScreen {
    data object Dashboard : TeacherScreen
    data object Sessions : TeacherScreen
    data class SessionDetail(val id: Int) : TeacherScreen
    data object Roster : TeacherScreen
    data object Attendance : TeacherScreen
    data object Activities : TeacherScreen
    data object Exams : TeacherScreen
    data object Results : TeacherScreen
    data object Settings : TeacherScreen
}

class TeacherController(
    private val repo: TeacherRepository,
    private val prefs: LocalStore,
    private val scope: CoroutineScope,
) {
    var screen by mutableStateOf<TeacherScreen>(TeacherScreen.Dashboard)
    var state by mutableStateOf<TeacherState?>(null)
    var signedIn by mutableStateOf(repo.signedIn)
    var busy by mutableStateOf(false)
    var error by mutableStateOf<String?>(null)
    var notice by mutableStateOf<String?>(null)
    var address by mutableStateOf(prefs.baseUrl)
    var showCorrect by mutableStateOf(prefs.showCorrect)
    var detail by mutableStateOf<SubmissionDetail?>(null)
    var detailLoading by mutableStateOf(false)

    private var refreshing = false

    fun dismissMessages() {
        error = null
        notice = null
    }

    fun signIn(pin: String) {
        if (busy) return
        busy = true
        scope.launch {
            try {
                repo.login(pin)
                signedIn = true
                error = null
                state = repo.state()
            } catch (failure: Throwable) {
                error = describeFailure(failure)
            } finally {
                busy = false
            }
        }
    }

    fun refresh(silent: Boolean = false) {
        if (!signedIn || refreshing) return
        refreshing = true
        scope.launch {
            try {
                val snapshot = repo.state()
                state = snapshot
                showCorrect = snapshot.showCorrect
                if (!silent) error = null
            } catch (failure: Throwable) {
                error = describeFailure(failure)
                if (failure is ApiException && failure.status == 401) signedIn = false
            } finally {
                refreshing = false
            }
        }
    }

    private fun act(
        success: String? = null,
        failurePrefix: String? = null,
        block: suspend () -> Unit,
    ) {
        if (busy) return
        busy = true
        scope.launch {
            try {
                block()
                notice = success
                error = null
                state = repo.state()
            } catch (failure: Throwable) {
                if (failure is ApiException && failure.status == 401) signedIn = false
                val message = describeFailure(failure)
                error = if (failurePrefix != null) "$failurePrefix $message" else message
            } finally {
                busy = false
            }
        }
    }

    fun addStudents(raw: String, onDone: () -> Unit) {
        val rows = raw.lines().mapNotNull { line ->
            val parts = line.split(",", ";").map { it.trim() }.filter { it.isNotEmpty() }
            if (parts.size < 3) null else Triple(parts[0], parts[1], parts[2])
        }
        if (rows.isEmpty()) {
            error = "Add rows like 001, Ana Cruz, 9-A"
            return
        }
        act("Roster updated.") {
            repo.importStudents(rows)
            onDone()
        }
    }

    fun createActivity(title: String, description: String, instructions: String, target: String, onDone: () -> Unit) {
        act("Activity saved as draft.") {
            repo.createActivity(title, description, instructions, target)
            onDone()
        }
    }

    fun createExam(
        title: String,
        instructions: String,
        target: String,
        duration: Int,
        passing: Int,
        questions: List<NewQuestion>,
        onDone: () -> Unit,
    ) {
        act("Examination saved as draft.") {
            repo.createExam(title, instructions, target, duration, passing, questions)
            onDone()
        }
    }

    fun publish(kind: String, id: Int) = act("Published.") { repo.publish(kind, id) }

    fun complete(kind: String, id: Int) = act("Marked completed; any open session was closed.") {
        repo.complete(kind, id)
    }

    fun createSession(kind: String, id: Int, minutes: Int) {
        if (busy) return
        busy = true
        scope.launch {
            try {
                val sessionId = repo.createSession(kind, id, minutes)
                state = repo.state()
                screen = TeacherScreen.SessionDetail(sessionId)
                notice = "Session created."
                error = null
            } catch (failure: Throwable) {
                error = describeFailure(failure)
            } finally {
                busy = false
            }
        }
    }

    fun sessionAction(id: Int, action: String) = act(
        when (action) {
            "start" -> "Session started."
            "pause" -> "Session paused."
            "resume" -> "Session resumed."
            "end" -> "Session ended."
            else -> "Session updated."
        },
    ) {
        repo.sessionAction(id, action)
    }

    fun deleteSession(id: Int) = act("Session backed up on this device, then deleted.") {
        repo.deleteSession(id)
    }

    fun finishDeletion(id: Int) = act("Session backed up on this device, then deleted.") {
        repo.finishDeletion(id)
    }

    fun cancelDeletion(id: Int) = act("Deletion canceled. The session is available again.") {
        repo.cancelDeletion(id)
    }

    fun markAttendance(date: String, section: String, studentId: Int? = null, status: String? = null, markAll: String? = null) {
        act {
            repo.markAttendance(date, section, studentId, status, markAll)
        }
    }

    fun review(id: Int) = act("Marked reviewed.") { repo.review(id) }

    fun updateShowCorrect(value: Boolean) {
        showCorrect = value
        prefs.showCorrect = value
        act { repo.setShowCorrect(value) }
    }

    fun loadDetail(id: Int) {
        detailLoading = true
        scope.launch {
            try {
                detail = repo.submissionDetail(id)
            } catch (failure: Throwable) {
                error = describeFailure(failure)
            } finally {
                detailLoading = false
            }
        }
    }

    fun closeDetail() {
        detail = null
    }

    fun applyAddress(newAddress: String) {
        address = newAddress.trim()
        prefs.baseUrl = address
        repo.forget()
        signedIn = false
        state = null
        screen = TeacherScreen.Dashboard
        notice = null
        error = null
    }

    fun signOut() {
        repo.forget()
        signedIn = false
        state = null
        error = null
        notice = null
    }
}

@Composable
fun TeacherApp(
    repo: TeacherRepository,
    prefs: LocalStore,
    onExitRole: () -> Unit,
) {
    val scope = rememberCoroutineScope()
    val controller = remember { TeacherController(repo, prefs, scope) }
    val drawerState = androidx.compose.material3.rememberDrawerState(DrawerValue.Closed)

    LaunchedEffect(controller.signedIn) {
        while (controller.signedIn) {
            controller.refresh(silent = true)
            delay(3000)
        }
    }

    if (!controller.signedIn) {
        TeacherSignIn(controller, onExitRole)
        return
    }

    ModalNavigationDrawer(
        drawerState = drawerState,
        drawerContent = {
            ModalDrawerSheet(
                drawerContainerColor = Color.Transparent,
                modifier = Modifier.width(280.dp),
            ) {
                TeacherDrawer(controller, onExitRole) { scope.launch { drawerState.close() } }
            }
        },
    ) {
        Scaffold(
            containerColor = MaterialTheme.colorScheme.background,
            topBar = {
                TeacherTopBar(
                    title = titleFor(controller.screen),
                    connected = controller.error == null,
                    onMenu = { scope.launch { drawerState.open() } },
                )
            },
        ) { padding ->
            Box(Modifier.padding(padding).fillMaxSize()) {
                TeacherContent(controller, onExitRole)
            }
        }
    }
}

private fun titleFor(screen: TeacherScreen): String = when (screen) {
    TeacherScreen.Dashboard -> "Dashboard"
    TeacherScreen.Sessions -> "Sessions"
    TeacherScreen.Roster -> "Students"
    TeacherScreen.Attendance -> "Attendance"
    TeacherScreen.Activities -> "Activities"
    TeacherScreen.Exams -> "Examinations"
    TeacherScreen.Results -> "Results & history"
    TeacherScreen.Settings -> "Device & settings"
    is TeacherScreen.SessionDetail -> "Session"
}

@Composable
private fun TeacherTopBar(title: String, connected: Boolean, onMenu: () -> Unit) {
    Surface(color = MaterialTheme.colorScheme.surface, tonalElevation = 1.dp) {
        Row(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 8.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Surface(
                shape = RoundedCornerShape(10.dp),
                color = Color.Transparent,
                modifier = Modifier.clickable { onMenu() },
            ) {
                Icon(
                    Icons.Filled.Menu,
                    contentDescription = "Open navigation",
                    modifier = Modifier.padding(10.dp),
                )
            }
            Text(
                title,
                style = MaterialTheme.typography.titleMedium,
                modifier = Modifier.weight(1f).padding(horizontal = 6.dp),
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            ConnectionChip(connected, if (connected) "Connected" else "Offline")
            Spacer(Modifier.width(6.dp))
        }
    }
}

@Composable
private fun TeacherDrawer(
    controller: TeacherController,
    onExitRole: () -> Unit,
    onNavigate: () -> Unit,
) {
    Column(
        Modifier
            .fillMaxSize()
            .background(Forest.sidebar)
            .verticalScroll(rememberScrollState())
            .padding(vertical = 18.dp),
    ) {
        Row(Modifier.padding(horizontal = 18.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .size(40.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(Forest.primaryButton),
                contentAlignment = Alignment.Center,
            ) { Text("S", color = Color(0xFF052E16), fontWeight = FontWeight.Black) }
            Spacer(Modifier.width(10.dp))
            Column {
                Text("SNTCH", color = Forest.onSidebar, style = MaterialTheme.typography.titleLarge)
                Text(
                    "Teacher console",
                    color = Forest.onSidebarMuted,
                    style = MaterialTheme.typography.bodyMedium,
                )
            }
        }
        Spacer(Modifier.height(18.dp))

        DrawerGroup(
            "Overview",
            listOf(TeacherScreen.Dashboard to "Dashboard"),
            controller.screen,
        ) {
            controller.screen = it
            onNavigate()
        }
        DrawerGroup(
            "Manage",
            listOf(
                TeacherScreen.Roster to "Students",
                TeacherScreen.Activities to "Activities",
                TeacherScreen.Exams to "Examinations",
            ),
            controller.screen,
        ) {
            controller.screen = it
            onNavigate()
        }
        DrawerGroup(
            "Classroom",
            listOf(TeacherScreen.Sessions to "Sessions", TeacherScreen.Attendance to "Attendance"),
            controller.screen,
        ) {
            controller.screen = it
            onNavigate()
        }
        DrawerGroup("Review", listOf(TeacherScreen.Results to "Results & history"), controller.screen) {
            controller.screen = it
            onNavigate()
        }
        DrawerGroup("System", listOf(TeacherScreen.Settings to "Device & settings"), controller.screen) {
            controller.screen = it
            onNavigate()
        }

        Spacer(Modifier.height(20.dp))
        val device = controller.state?.device
        Column(Modifier.padding(horizontal = 18.dp)) {
            Text("CONTROLLER", style = MaterialTheme.typography.labelSmall, color = Forest.onSidebarMuted)
            Spacer(Modifier.height(6.dp))
            Text(
                device?.name?.takeIf { it.isNotEmpty() } ?: "Not connected",
                color = Forest.onSidebar,
                style = MaterialTheme.typography.bodyLarge,
            )
            Text(
                device?.ip?.takeIf { it.isNotEmpty() } ?: controller.address,
                color = Forest.onSidebarMuted,
                style = MaterialTheme.typography.bodyMedium,
            )
            Spacer(Modifier.height(10.dp))
            QuietButton("Sign out", { controller.signOut() })
            Spacer(Modifier.height(8.dp))
            QuietButton("Switch role", { onExitRole(); onNavigate() })
        }
    }
}

@Composable
private fun DrawerGroup(
    label: String,
    entries: List<Pair<TeacherScreen, String>>,
    current: TeacherScreen,
    onSelect: (TeacherScreen) -> Unit,
) {
    Text(
        label.uppercase(),
        style = MaterialTheme.typography.labelSmall,
        color = Forest.onSidebarMuted,
        modifier = Modifier.padding(horizontal = 20.dp, vertical = 8.dp),
    )
    entries.forEach { (destination, text) ->
        val selected = if (destination is TeacherScreen.SessionDetail) {
            current is TeacherScreen.SessionDetail
        } else {
            current == destination
        }
        Row(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = 10.dp, vertical = 2.dp)
                .clip(RoundedCornerShape(12.dp))
                .background(if (selected) Color(0xFF166342) else Color.Transparent)
                .clickable { onSelect(destination) }
                .padding(horizontal = 12.dp, vertical = 12.dp),
        ) {
            Text(
                text,
                color = if (selected) Forest.onSidebar else Color(0xFFE7FFF0),
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
            )
        }
    }
}

@Composable
private fun TeacherContent(controller: TeacherController, onExitRole: () -> Unit) {
    val state = controller.state
    if (state == null) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                CircularProgressIndicator(color = Forest.emerald)
                Spacer(Modifier.height(10.dp))
                Text(
                    controller.error ?: "Loading classroom…",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(12.dp))
                QuietButton("Retry", { controller.refresh() })
            }
        }
        return
    }

    when (val screen = controller.screen) {
        TeacherScreen.Dashboard -> DashboardScreen(state, controller)
        TeacherScreen.Sessions -> SessionsScreen(state, controller)
        is TeacherScreen.SessionDetail -> SessionDetailScreen(state, screen.id, controller)
        TeacherScreen.Roster -> RosterScreen(state, controller)
        TeacherScreen.Attendance -> AttendanceScreen(state, controller)
        TeacherScreen.Activities -> ItemsScreen(state, controller, exam = false)
        TeacherScreen.Exams -> ItemsScreen(state, controller, exam = true)
        TeacherScreen.Results -> ResultsScreen(state, controller)
        TeacherScreen.Settings -> SettingsScreen(controller, onExitRole)
    }
}

// ------------------------------------------------------------------ shared --

@Composable
private fun ScreenBody(content: @Composable () -> Unit) {
    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        content()
    }
}

@Composable
private fun Messages(controller: TeacherController) {
    controller.notice?.let { text ->
        Surface(
            color = Forest.okBg,
            shape = RoundedCornerShape(12.dp),
            modifier = Modifier.fillMaxWidth().clickable { controller.notice = null },
        ) {
            Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(text, color = Forest.okFg, modifier = Modifier.weight(1f))
                Icon(Icons.Filled.Close, contentDescription = "Dismiss", tint = Forest.okFg, modifier = Modifier.size(16.dp))
            }
        }
    }
    controller.error?.let { text ->
        Surface(
            color = Forest.badBg,
            shape = RoundedCornerShape(12.dp),
            modifier = Modifier.fillMaxWidth().clickable { controller.error = null },
        ) {
            Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(text, color = Forest.badFg, modifier = Modifier.weight(1f))
                Icon(Icons.Filled.Close, contentDescription = "Dismiss", tint = Forest.badFg, modifier = Modifier.size(16.dp))
            }
        }
    }
}

@Composable
private fun ItemRow(
    title: String,
    meta: String,
    badge: String? = null,
    onClick: (() -> Unit)? = null,
) {
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(10.dp))
            .let { if (onClick != null) it.clickable { onClick() } else it }
            .padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.bodyLarge, maxLines = 2, overflow = TextOverflow.Ellipsis)
            Text(
                meta,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Spacer(Modifier.width(8.dp))
        if (badge != null) Badge(badge, statusTone(badge.lowercase()))
    }
}

// --------------------------------------------------------------- dashboard --

@Composable
private fun DashboardScreen(state: TeacherState, controller: TeacherController) {
    val openSessions = state.sessions.filter { it.open }
    val pendingReviews = state.submissions.count { it.refType == "activity" && it.status == "pending" }
    val today = todayIso()

    ScreenBody {
        Messages(controller)

        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatTile("Students", state.students.size.toString(), "on the roster", Modifier.weight(1f))
            StatTile("Open sessions", openSessions.size.toString(), "live or waiting", Modifier.weight(1f))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatTile("Activities & exams", state.items().size.toString(), "in the controller", Modifier.weight(1f))
            StatTile("Pending review", pendingReviews.toString(), "activity responses", Modifier.weight(1f))
        }

        SntchCard(title = "Controller", action = {
            Badge(
                if (state.device.storageError) "Storage error" else "Connected",
                if (state.device.storageError) Tone.BAD else Tone.OK,
            )
        }) {
            Text(
                state.device.name.ifEmpty { "SNTCH controller" },
                style = MaterialTheme.typography.titleMedium,
            )
            Text(
                "Network ${state.device.ssid.ifEmpty { "—" }} · ${state.device.ip.ifEmpty { controller.address }}",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(10.dp))
            val used = if (state.device.storageTotal > 0) {
                state.device.storageUsed.toFloat() / state.device.storageTotal
            } else 0f
            Text(
                "Storage ${state.device.storageUsed / 1024} KB of ${state.device.storageTotal / 1024} KB",
                style = MaterialTheme.typography.bodyMedium,
            )
            Spacer(Modifier.height(6.dp))
            ProgressBar(used)
            Spacer(Modifier.height(8.dp))
            Text(
                "Firmware ${state.device.firmware.ifEmpty { "—" }} · free heap ${state.device.freeHeap / 1024} KB",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        SntchCard(
            title = "Open sessions",
            action = { QuietButton("Manage", { controller.screen = TeacherScreen.Sessions }) },
        ) {
            if (openSessions.isEmpty()) {
                EmptyState(
                    "No sessions running",
                    "Publish an activity or examination, then open a session so students can join.",
                    "Create session",
                ) { controller.screen = TeacherScreen.Sessions }
            } else {
                openSessions.take(4).forEach { session ->
                    ItemRow(
                        title = session.title,
                        meta = "${session.refType.replaceFirstChar { it.uppercase() }} · section ${session.target} · ${session.joined.size} joined",
                        badge = session.state,
                    ) { controller.screen = TeacherScreen.SessionDetail(session.id) }
                }
            }
        }

        SntchCard(
            title = "Attendance today",
            action = { QuietButton("Open", { controller.screen = TeacherScreen.Attendance }) },
        ) {
            val sections = state.students.map { it.section }.distinct().sorted()
            if (sections.isEmpty()) {
                EmptyState("No students yet", "Import your roster to start tracking attendance.")
            } else {
                sections.forEach { section ->
                    val roster = state.studentsIn(section)
                    val marks = state.attendanceFor(today, section)
                    val present = roster.count { marks[it.id] == "present" }
                    val late = roster.count { marks[it.id] == "late" }
                    val absent = roster.count { marks[it.id] == "absent" }
                    val unmarked = roster.size - present - late - absent
                    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                        Text("Section $section", modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
                        Text(
                            "$present present · $late late · $absent absent · $unmarked unmarked",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }

        SntchCard(title = "Recent activity") {
            if (state.events.isEmpty()) {
                EmptyState("Nothing yet", "Actions you take on the controller appear here.")
            } else {
                state.events.take(6).forEach { event ->
                    Row(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
                        Box(
                            Modifier
                                .padding(top = 6.dp)
                                .size(8.dp)
                                .clip(RoundedCornerShape(50))
                                .background(
                                    when (event.tone) {
                                        "ok" -> Forest.okFg
                                        "warn" -> Forest.warnFg
                                        "bad" -> Forest.badFg
                                        else -> Forest.infoFg
                                    },
                                ),
                        )
                        Spacer(Modifier.width(10.dp))
                        Text(event.text, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                        Text(
                            uptimeAgo(state.device.uptimeMs, event.atMs),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------- sessions --

@Composable
private fun SessionsScreen(state: TeacherState, controller: TeacherController) {
    var showCreate by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf<Session?>(null) }
    val open = state.sessions.filter { it.open }
    val past = state.sessions.filter { !it.open }

    ScreenBody {
        Messages(controller)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(
                "${open.size} open · ${past.size} closed",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.weight(1f),
            )
            PrimaryButton("New session", { showCreate = true }, enabled = !controller.busy)
        }

        if (state.sessions.isEmpty()) {
            SntchCard {
                EmptyState(
                    "No sessions yet",
                    "Sessions are how students join. Publish an item first, then open a session for it.",
                    "New session",
                ) { showCreate = true }
            }
        } else {
            state.sessions.forEach { session ->
                SntchCard {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(session.title, style = MaterialTheme.typography.titleMedium)
                            Text(
                                "${session.refType.replaceFirstChar { it.uppercase() }} · section ${session.target} · ${session.joined.size} joined",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Badge(statusLabel(session.state), statusTone(session.state))
                    }
                    Spacer(Modifier.height(10.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        CodePill(session.code)
                        Spacer(Modifier.weight(1f))
                        QuietButton("Open", { controller.screen = TeacherScreen.SessionDetail(session.id) })
                    }
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        if (session.state == "ready") {
                            QuietButton("Start", { controller.sessionAction(session.id, "start") })
                        }
                        if (session.state == "live") {
                            QuietButton("Pause", { controller.sessionAction(session.id, "pause") })
                        }
                        if (session.state == "paused") {
                            QuietButton("Resume", { controller.sessionAction(session.id, "resume") })
                        }
                        if (session.open) {
                            QuietButton("End", { controller.sessionAction(session.id, "end") })
                        }
                        if (session.pendingDelete) {
                            QuietButton("Retry delete", { controller.finishDeletion(session.id) })
                            QuietButton("Cancel delete", { controller.cancelDeletion(session.id) })
                        } else if (session.deleteEligible) {
                            QuietButton("Delete", { confirmDelete = session }, enabled = !controller.busy)
                        }
                    }
                }
            }
        }
    }

    if (showCreate) {
        CreateSessionDialog(state, controller) { showCreate = false }
    }

    confirmDelete?.let { session ->
        ConfirmDialog(
            title = "Delete session?",
            message = "A full backup of \"${session.title}\" will be saved on this device before the controller removes it. Students can no longer join it.",
            confirmText = "Delete",
            onConfirm = {
                confirmDelete = null
                controller.deleteSession(session.id)
            },
            onDismiss = { confirmDelete = null },
        )
    }
}

@Composable
private fun CreateSessionDialog(state: TeacherState, controller: TeacherController, onDismiss: () -> Unit) {
    val available = state.items().filter { it.status == "active" }
    var selected by remember { mutableStateOf(available.firstOrNull()) }
    var minutes by remember { mutableStateOf("30") }

    SntchDialog(
        title = "New session",
        onDismiss = onDismiss,
        confirmText = "Create session",
        confirmEnabled = selected != null && (minutes.toIntOrNull() ?: 0) in 5..180,
        onConfirm = {
            val item = selected
            if (item != null) {
                controller.createSession(item.kind, item.id, minutes.toIntOrNull() ?: 30)
                onDismiss()
            }
        },
    ) {
        if (available.isEmpty()) {
            InfoBanner("Publish an activity or examination first — a session needs an active item.", Tone.WARN)
        }
        available.forEach { item ->
            Surface(
                color = if (selected?.id == item.id && selected?.kind == item.kind)
                    Forest.okBg else MaterialTheme.colorScheme.surfaceVariant,
                shape = RoundedCornerShape(12.dp),
                modifier = Modifier.fillMaxWidth().clickable { selected = item },
            ) {
                Column(Modifier.padding(12.dp)) {
                    Text(item.title, style = MaterialTheme.typography.titleMedium)
                    Text(
                        "${item.kind.replaceFirstChar { it.uppercase() }} · section ${item.target}",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
        Field(minutes, { minutes = it.filter(Char::isDigit) }, "Session length (minutes)", keyboardType = KeyboardType.Number)
        Text(
            "Valid for 5–180 minutes. The controller has no clock, so sessions expire by duration.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun SessionDetailScreen(state: TeacherState, sessionId: Int, controller: TeacherController) {
    val session = state.session(sessionId)
    if (session == null) {
        ScreenBody {
            Messages(controller)
            EmptyState("Session not found", "It may have been pruned after the controller restarted.")
            QuietButton("Back to sessions", { controller.screen = TeacherScreen.Sessions })
        }
        return
    }

    var confirmDelete by remember { mutableStateOf(false) }
    val roster = state.studentsIn(session.target)
    val subs = state.submissionsFor(session.refType, session.refId)
    val joined = roster.count { session.joined.contains(it.id) }
    val responded = roster.count { student -> subs.any { it.studentId == student.id } }
    val examSubs = subs.filter { it.refType == "exam" }
    val average = if (examSubs.isEmpty()) null else examSubs.sumOf { it.percent } / examSubs.size

    ScreenBody {
        Messages(controller)
        Row(verticalAlignment = Alignment.CenterVertically) {
            QuietButton("Back", { controller.screen = TeacherScreen.Sessions })
            Spacer(Modifier.weight(1f))
            Badge(statusLabel(session.state), statusTone(session.state))
        }
        Text(session.title, style = MaterialTheme.typography.headlineSmall)
        Text(
            "${session.refType.replaceFirstChar { it.uppercase() }} · section ${session.target}",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        SntchCard(title = "Join information") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                CodePill(session.code, big = true)
                Spacer(Modifier.weight(1f))
                if (session.state == "ready") PrimaryButton("Start", { controller.sessionAction(session.id, "start") })
            }
            Spacer(Modifier.height(10.dp))
            Text(
                "1. Join the SNTCH Wi-Fi network.\n2. Open SNTCH and choose Student.\n3. Enter this code and your student number.",
                style = MaterialTheme.typography.bodyMedium,
            )
            Spacer(Modifier.height(8.dp))
            Text(
                if (session.open) "Expires in ${session.expiresInSec / 60} min."
                else "This session is closed.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            if (session.state == "live") QuietButton("Pause", { controller.sessionAction(session.id, "pause") })
            if (session.state == "paused") QuietButton("Resume", { controller.sessionAction(session.id, "resume") })
            if (session.open) QuietButton("End session", { controller.sessionAction(session.id, "end") })
            if (session.pendingDelete) {
                QuietButton("Retry delete", { controller.finishDeletion(session.id) })
                QuietButton("Cancel delete", { controller.cancelDeletion(session.id) })
            } else if (session.deleteEligible) {
                QuietButton("Delete", { confirmDelete = true }, enabled = !controller.busy)
            }
        }

        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatTile("Joined", "$joined/${roster.size}", "students", Modifier.weight(1f))
            StatTile("Responded", responded.toString(), "of joined", Modifier.weight(1f))
            StatTile("Average", average?.let { "$it%" } ?: "—", "exam scores", Modifier.weight(1f))
        }

        SntchCard(title = "Participants") {
            if (roster.isEmpty()) {
                EmptyState("No students in section ${session.target}", "Import students for this section first.")
            } else {
                roster.forEach { student ->
                    val submission = subs.firstOrNull { it.studentId == student.id }
                    Row(Modifier.fillMaxWidth().padding(vertical = 7.dp), verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(student.name, style = MaterialTheme.typography.bodyLarge)
                            Text(
                                if (session.joined.contains(student.id)) "Joined" else "Not joined",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        if (submission == null) {
                            Badge("Waiting", Tone.MUTED)
                        } else if (submission.isExam) {
                            Text(
                                "${submission.score}/${submission.total} · ${submission.percent}%",
                                style = MaterialTheme.typography.bodyMedium,
                                fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                            )
                            Spacer(Modifier.width(6.dp))
                            Badge(if (submission.passed) "Passed" else "Failed", if (submission.passed) Tone.OK else Tone.BAD)
                        } else {
                            Badge(statusLabel(submission.status), statusTone(submission.status))
                        }
                    }
                }
            }
        }
    }

    if (confirmDelete) {
        ConfirmDialog(
            title = "Delete session?",
            message = "A full backup of \"${session.title}\" will be saved on this device before the controller removes it. This session and its participants will disappear from the controller.",
            confirmText = "Delete",
            onConfirm = {
                confirmDelete = false
                controller.deleteSession(session.id)
            },
            onDismiss = { confirmDelete = false },
        )
    }

    controller.detail?.let { detail ->
        SubmissionDialog(detail, state, controller)
    }
}

// ------------------------------------------------------------------ roster --

@Composable
private fun RosterScreen(state: TeacherState, controller: TeacherController) {
    var query by remember { mutableStateOf("") }
    var section by remember { mutableStateOf("all") }
    var showAdd by remember { mutableStateOf(false) }
    val sections = state.students.map { it.section }.distinct().sorted()
    val rows = state.students.filter { student ->
        (section == "all" || student.section == section) &&
            (query.isBlank() || student.name.contains(query, true) || student.number.contains(query, true))
    }

    ScreenBody {
        Messages(controller)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(
                "${state.students.size} students",
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            PrimaryButton("Add students", { showAdd = true })
        }
        Field(query, { query = it }, "Search name or number")
        if (sections.isNotEmpty()) {
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                SelectChip("All", section == "all") { section = "all" }
                sections.forEach { value ->
                    SelectChip(value, section == value) { section = value }
                }
            }
        }
        SntchCard {
            if (rows.isEmpty()) {
                EmptyState("No students match", "Adjust the filter or add students to the controller.")
            } else {
                rows.forEach { student ->
                    val today = state.attendanceFor(todayIso(), student.section)[student.id] ?: "unmarked"
                    val submissions = state.submissions.count { it.studentId == student.id }
                    Row(Modifier.fillMaxWidth().padding(vertical = 7.dp), verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(student.name, style = MaterialTheme.typography.bodyLarge)
                            Text(
                                "${student.number} · section ${student.section} · $submissions submission(s)",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Badge(statusLabel(today), statusTone(today))
                    }
                }
            }
        }
    }

    if (showAdd) {
        var text by remember {
            mutableStateOf("001, Ana Cruz, 9-A\n002, Ben Lim, 9-A")
        }
        SntchDialog(
            title = "Add or update students",
            onDismiss = { showAdd = false },
            confirmText = "Save roster",
            onConfirm = {
                controller.addStudents(text) { showAdd = false }
            },
        ) {
            Text(
                "One student per line: number, name, section. Importing an existing number updates that student.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Field(
                text,
                { text = it },
                "Rows",
                singleLine = false,
                minLines = 6,
                maxLines = 10,
            )
        }
    }
}

// -------------------------------------------------------------- attendance --

@Composable
private fun AttendanceScreen(state: TeacherState, controller: TeacherController) {
    val sections = state.students.map { it.section }.distinct().sorted()
    var section by remember { mutableStateOf(sections.firstOrNull() ?: "9-A") }
    var date by remember { mutableStateOf(todayIso()) }
    var query by remember { mutableStateOf("") }

    val roster = state.studentsIn(section).filter {
        query.isBlank() || it.name.contains(query, true) || it.number.contains(query, true)
    }
    val marks = state.attendanceFor(date, section)
    val present = state.studentsIn(section).count { marks[it.id] == "present" }
    val late = state.studentsIn(section).count { marks[it.id] == "late" }
    val absent = state.studentsIn(section).count { marks[it.id] == "absent" }
    val unmarked = state.studentsIn(section).size - present - late - absent

    ScreenBody {
        Messages(controller)
        if (sections.isEmpty()) {
            SntchCard { EmptyState("No students yet", "Import the roster before taking attendance.") }
            return@ScreenBody
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            sections.forEach { value -> SelectChip(value, section == value) { section = value } }
        }
        Field(
            date,
            { date = it },
            "Date (YYYY-MM-DD)",
            placeholder = "2026-10-25",
            supporting = "Attendance dates come from this phone's clock; the controller has no real-time clock.",
        )
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatTile("Present", present.toString(), null, Modifier.weight(1f))
            StatTile("Late", late.toString(), null, Modifier.weight(1f))
            StatTile("Absent", absent.toString(), null, Modifier.weight(1f))
            StatTile("Unmarked", unmarked.toString(), null, Modifier.weight(1f))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            QuietButton("Mark unmarked present", onClick = {
                controller.markAttendance(date, section, markAll = "present")
            })
            QuietButton("Clear section", onClick = {
                controller.markAttendance(date, section, markAll = "unmarked")
            })
        }
        Field(query, { query = it }, "Search students")
        SntchCard {
            if (roster.isEmpty()) {
                EmptyState("No students match", "Clear the search to see the whole section.")
            } else {
                roster.forEach { student ->
                    val status = marks[student.id] ?: "unmarked"
                    Column(Modifier.fillMaxWidth().padding(vertical = 8.dp)) {
                        Text(student.name, style = MaterialTheme.typography.bodyLarge)
                        Text(
                            student.number,
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        Spacer(Modifier.height(6.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                            listOf("present", "late", "absent").forEach { value ->
                                SelectChip(statusLabel(value), status == value) {
                                    controller.markAttendance(date, section, student.id, value)
                                }
                            }
                        }
                    }
                }
            }
        }
        val history = state.attendance.filter { it.section == section }.sortedByDescending { it.date }
        SntchCard(title = "Recent history") {
            if (history.isEmpty()) {
                EmptyState("No history yet", "Saved dates appear here.")
            } else {
                history.take(7).forEach { day: AttendanceDay ->
                    val p = day.marks.values.count { it == "present" }
                    val l = day.marks.values.count { it == "late" }
                    val a = day.marks.values.count { it == "absent" }
                    Row(Modifier.fillMaxWidth().padding(vertical = 6.dp)) {
                        Text(dateText(day.date), modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                        Text(
                            "$p present · $l late · $a absent",
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }
    }
}

// ------------------------------------------------------- activities / exams --

@Composable
private fun ItemsScreen(state: TeacherState, controller: TeacherController, exam: Boolean) {
    val items = if (exam) state.exams else state.activities
    var showCreate by remember { mutableStateOf(false) }
    var sessionFor by remember { mutableStateOf<Item?>(null) }
    val sections = state.students.map { it.section }.distinct().sorted()

    ScreenBody {
        Messages(controller)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                if (exam) "Question banks, publish, run" else "Draft, publish, run",
                modifier = Modifier.weight(1f),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            PrimaryButton(if (exam) "New exam" else "New activity", { showCreate = true })
        }

        if (items.isEmpty()) {
            SntchCard {
                EmptyState(
                    if (exam) "No examinations yet" else "No activities yet",
                    if (exam) "Build a question set, publish it, then open a session."
                    else "Create a short task, publish it, then open a session.",
                    if (exam) "New examination" else "New activity",
                ) { showCreate = true }
            }
        } else {
            items.forEach { item ->
                SntchCard {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(item.title, style = MaterialTheme.typography.titleMedium)
                            Text(
                                "Section ${item.target}" + if (exam) " · ${item.questionCount} questions · ${item.durationMin} min" else "",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Badge(statusLabel(item.status), statusTone(item.status))
                    }
                    if (!exam && item.description.isNotBlank()) {
                        Spacer(Modifier.height(6.dp))
                        Text(item.description, style = MaterialTheme.typography.bodyMedium)
                    }
                    Spacer(Modifier.height(10.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        if (item.status == "draft") {
                            PrimaryButton("Publish", {
                                controller.publish(item.kind, item.id)
                            }, enabled = !controller.busy)
                        }
                        if (item.status == "active") {
                            QuietButton("Start session", { sessionFor = item })
                        }
                        if (item.status != "completed") {
                            QuietButton("Complete", { controller.complete(item.kind, item.id) })
                        }
                    }
                }
            }
        }
    }

    if (showCreate) {
        if (exam) {
            ExamBuilder(state, controller) { showCreate = false }
        } else {
            ActivityBuilder(controller, sections, onDismiss = { showCreate = false })
        }
    }

    sessionFor?.let { item ->
        var minutes by remember(item.id) { mutableStateOf("30") }
        SntchDialog(
            title = "Open a session",
            onDismiss = { sessionFor = null },
            confirmText = "Create session",
            confirmEnabled = (minutes.toIntOrNull() ?: 0) in 5..180,
            onConfirm = {
                controller.createSession(item.kind, item.id, minutes.toIntOrNull() ?: 30)
                sessionFor = null
            },
        ) {
            Text(item.title, style = MaterialTheme.typography.titleMedium)
            Field(minutes, { minutes = it.filter(Char::isDigit) }, "Session length (minutes)", keyboardType = KeyboardType.Number)
        }
    }
}

@Composable
private fun ActivityBuilder(controller: TeacherController, sections: List<String>, onDismiss: () -> Unit) {
    var title by remember { mutableStateOf("") }
    var description by remember { mutableStateOf("") }
    var instructions by remember { mutableStateOf("") }
    var target by remember { mutableStateOf(sections.firstOrNull() ?: "9-A") }

    SntchDialog(
        title = "New activity",
        onDismiss = onDismiss,
        confirmText = "Save draft",
        confirmEnabled = title.isNotBlank() && target.isNotBlank(),
        onConfirm = {
            controller.createActivity(title, description, instructions, target, onDismiss)
        },
    ) {
        Field(title, { title = it }, "Title")
        if (sections.isNotEmpty()) {
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                sections.forEach { value -> SelectChip(value, target == value) { target = value } }
            }
        } else {
            Field(target, { target = it }, "Section")
        }
        Field(description, { description = it }, "Short description", singleLine = false, minLines = 2)
        Field(instructions, { instructions = it }, "Instructions for students", singleLine = false, minLines = 3)
    }
}

@Composable
private fun ExamBuilder(state: TeacherState, controller: TeacherController, onDismiss: () -> Unit) {
    val sections = state.students.map { it.section }.distinct().sorted()
    var title by remember { mutableStateOf("") }
    var instructions by remember { mutableStateOf("") }
    var target by remember { mutableStateOf(sections.firstOrNull() ?: "9-A") }
    var duration by remember { mutableStateOf("15") }
    var passing by remember { mutableStateOf("60") }
    val questions = remember { androidx.compose.runtime.mutableStateListOf<NewQuestion>() }
    var addQuestion by remember { mutableStateOf(false) }

    SntchDialog(
        title = "New examination",
        onDismiss = onDismiss,
        confirmText = "Create exam",
        confirmEnabled = title.isNotBlank() && questions.isNotEmpty() &&
            (duration.toIntOrNull() ?: 0) in 1..120 && (passing.toIntOrNull() ?: -1) in 0..100,
        onConfirm = {
            controller.createExam(
                title,
                instructions,
                target,
                duration.toIntOrNull() ?: 15,
                passing.toIntOrNull() ?: 60,
                questions.toList(),
                onDismiss,
            )
        },
    ) {
        Field(title, { title = it }, "Title")
        if (sections.isNotEmpty()) {
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                sections.forEach { value -> SelectChip(value, target == value) { target = value } }
            }
        } else {
            Field(target, { target = it }, "Section")
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Field(duration, { duration = it.filter(Char::isDigit) }, "Minutes", Modifier.weight(1f), keyboardType = KeyboardType.Number)
            Field(passing, { passing = it.filter(Char::isDigit) }, "Pass %", Modifier.weight(1f), keyboardType = KeyboardType.Number)
        }
        Field(instructions, { instructions = it }, "Instructions", singleLine = false, minLines = 2)
        Text("Questions (${questions.size})", style = MaterialTheme.typography.titleMedium)
        questions.forEachIndexed { index, question ->
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("${index + 1}. ${question.prompt}", style = MaterialTheme.typography.bodyMedium)
                    Text(
                        if (question.type == "mc") "Multiple choice · ${question.options.size} options"
                        else "Short text · accepts ${question.keys.size} answer(s)",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Surface(shape = RoundedCornerShape(50), modifier = Modifier.clickable { questions.removeAt(index) }) {
                    Icon(Icons.Filled.Close, contentDescription = "Remove question", modifier = Modifier.padding(6.dp).size(16.dp))
                }
            }
        }
        QuietButton("Add question", { addQuestion = true })
    }

    if (addQuestion) {
        QuestionBuilder(onDismiss = { addQuestion = false }) { question ->
            questions.add(question)
            addQuestion = false
        }
    }
}

@Composable
private fun QuestionBuilder(onDismiss: () -> Unit, onAdd: (NewQuestion) -> Unit) {
    var type by remember { mutableStateOf("mc") }
    var prompt by remember { mutableStateOf("") }
    val options = remember { androidx.compose.runtime.mutableStateListOf("", "") }
    var correct by remember { mutableStateOf(0) }
    var keys by remember { mutableStateOf("") }

    val valid = prompt.isNotBlank() &&
        if (type == "mc") options.size >= 2 && options.all { it.isNotBlank() }
        else keys.split(",").any { it.isNotBlank() }

    SntchDialog(
        title = "Add question",
        onDismiss = onDismiss,
        confirmText = "Add",
        confirmEnabled = valid,
        onConfirm = {
            if (type == "mc") {
                onAdd(NewQuestion(type = "mc", prompt = prompt, options = options.toList(), correct = correct))
            } else {
                onAdd(
                    NewQuestion(
                        type = "text",
                        prompt = prompt,
                        keys = keys.split(",").map { it.trim() }.filter { it.isNotEmpty() },
                    ),
                )
            }
        },
    ) {
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            SelectChip("Multiple choice", type == "mc") { type = "mc" }
            SelectChip("Short text", type == "text") { type = "text" }
        }
        Field(prompt, { prompt = it }, "Question prompt", singleLine = false, minLines = 2)
        if (type == "mc") {
            options.forEachIndexed { index, value ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Field(
                        value,
                        { options[index] = it },
                        "Option ${('A' + index)}",
                        modifier = Modifier.weight(1f),
                    )
                    Spacer(Modifier.width(6.dp))
                    SelectChip(if (correct == index) "Key" else "Set", correct == index) { correct = index }
                }
            }
            if (options.size < 6) {
                QuietButton("Add option", { options.add("") })
            }
        } else {
            Field(keys, { keys = it }, "Accepted answers (comma separated)")
        }
    }
}

// ----------------------------------------------------------------- results --

@Composable
private fun ResultsScreen(state: TeacherState, controller: TeacherController) {
    val examSubs = state.submissions.filter { it.isExam }
    val average = if (examSubs.isEmpty()) null else examSubs.sumOf { it.percent } / examSubs.size
    val passRate = if (examSubs.isEmpty()) null else examSubs.count { it.passed } * 100 / examSubs.size
    var filter by remember { mutableStateOf("all") }

    val rows = state.submissions
        .filter { filter == "all" || it.refType == filter }
        .sortedByDescending { it.id }

    ScreenBody {
        Messages(controller)
        Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            StatTile("Submissions", state.submissions.size.toString(), "on the controller", Modifier.weight(1f))
            StatTile("Exam average", average?.let { "$it%" } ?: "—", "graded exams", Modifier.weight(1f))
            StatTile("Pass rate", passRate?.let { "$it%" } ?: "—", "against pass marks", Modifier.weight(1f))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            SelectChip("All", filter == "all") { filter = "all" }
            SelectChip("Exams", filter == "exam") { filter = "exam" }
            SelectChip("Activities", filter == "activity") { filter = "activity" }
        }
        SntchCard {
            if (rows.isEmpty()) {
                EmptyState("No results yet", "Submissions appear here as students respond.")
            } else {
                rows.forEach { submission ->
                    SubmissionRow(submission) { controller.loadDetail(submission.id) }
                }
            }
        }
        if (state.submissions.any { it.refType == "activity" && it.status == "pending" }) {
            InfoBanner("Activity responses marked pending are waiting for your review.", Tone.WARN)
        }
    }

    if (controller.detailLoading) {
        InfoBanner("Loading submission…", Tone.INFO)
    }
    controller.detail?.let { detail -> SubmissionDialog(detail, state, controller) }
}

@Composable
private fun SubmissionRow(submission: Submission, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().clickable { onClick() }.padding(vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(
                submission.studentName.ifEmpty { "Student ${submission.studentId}" },
                style = MaterialTheme.typography.bodyLarge,
            )
            Text(
                "${submission.title.ifEmpty { submission.refType }} · ${submission.refType}",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        if (submission.isExam) {
            Column(horizontalAlignment = Alignment.End) {
                Text(
                    "${submission.percent}%",
                    style = MaterialTheme.typography.titleMedium,
                    fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                )
                Badge(if (submission.passed) "Passed" else "Failed", if (submission.passed) Tone.OK else Tone.BAD)
            }
        } else {
            Badge(statusLabel(submission.status), statusTone(submission.status))
        }
    }
}

@Composable
private fun SubmissionDialog(detail: SubmissionDetail, state: TeacherState, controller: TeacherController) {
    val item = state.item(detail.summary.refType, detail.summary.refId)
    SntchDialog(
        title = "Submission",
        onDismiss = { controller.closeDetail() },
        confirmText = if (detail.summary.refType == "activity" && detail.summary.status == "pending") "Mark reviewed" else null,
        onConfirm = {
            controller.review(detail.summary.id)
            controller.loadDetail(detail.summary.id)
        },
        dismissText = "Close",
    ) {
        Text(
            "${detail.summary.studentName} · ${detail.summary.title}",
            style = MaterialTheme.typography.titleMedium,
        )
        if (detail.summary.isExam) {
            Text(
                "${detail.summary.score}/${detail.summary.total} · ${detail.summary.percent}% · " +
                    if (detail.summary.passed) "passed" else "failed",
                style = MaterialTheme.typography.bodyLarge,
            )
            if (detail.summary.timeout) {
                InfoBanner("Automatically submitted when time ran out or the session ended.", Tone.WARN)
            }
            detail.answers.forEach { (questionId, answer) ->
                val question = detail.questions.firstOrNull { it.id.toString() == questionId }
                    ?: item?.questions?.firstOrNull { it.id.toString() == questionId }
                Text(
                    question?.prompt ?: "Question $questionId",
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.SemiBold,
                )
                val shown = if (answer.isBlank()) {
                    "No answer"
                } else if (question != null && question.isMultipleChoice) {
                    val index = answer.toIntOrNull() ?: -1
                    val option = question.options.getOrNull(index)
                    val mark = if (index == question.correct) " (correct)" else ""
                    (option ?: answer) + mark
                } else {
                    answer
                }
                Text(shown, style = MaterialTheme.typography.bodyMedium)
            }
        } else {
            Badge(statusLabel(detail.summary.status), statusTone(detail.summary.status))
            Text(detail.text.ifEmpty { "(empty response)" }, style = MaterialTheme.typography.bodyLarge)
        }
        Text(
            "Stored on the controller. Times use controller uptime, not wall clock.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

// ---------------------------------------------------------------- settings --

@Composable
private fun SettingsScreen(controller: TeacherController, onExitRole: () -> Unit) {
    var addressDraft by remember { mutableStateOf(controller.address) }
    var confirmAddress by remember { mutableStateOf(false) }
    val state = controller.state

    ScreenBody {
        Messages(controller)
        SntchCard(title = "Controller address") {
            Field(addressDraft, { addressDraft = it }, "Base URL", keyboardType = KeyboardType.Uri)
            Text(
                "Default is http://192.168.4.1 — the controller's access point. Changing this signs you out.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Row {
                QuietButton("Apply", { confirmAddress = true }, enabled = addressDraft != controller.address)
            }
        }

        SntchCard(title = "Exam results policy") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("Show correct answers to students", style = MaterialTheme.typography.bodyLarge)
                    Text(
                        "Applied by the student app after submission.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Switch(checked = controller.showCorrect, onCheckedChange = { controller.updateShowCorrect(it) })
            }
        }

        if (state != null) {
            SntchCard(title = "Device") {
                val rows = listOf(
                    "Name" to state.device.name,
                    "Network" to state.device.ssid,
                    "Address" to state.device.ip,
                    "Firmware" to state.device.firmware,
                    "Uptime" to "${state.device.uptimeMs / 60000} min",
                    "Free heap" to "${state.device.freeHeap / 1024} KB",
                    "Storage" to "${state.device.storageUsed / 1024} / ${state.device.storageTotal / 1024} KB",
                )
                rows.forEach { (label, value) ->
                    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                        Text(label, modifier = Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                        Text(
                            value.ifEmpty { "—" },
                            style = MaterialTheme.typography.bodyMedium,
                            fontFamily = androidx.compose.ui.text.font.FontFamily.Monospace,
                        )
                    }
                }
                if (state.device.storageError) {
                    InfoBanner("The controller could not read its saved state. Data may not persist.", Tone.BAD)
                }
            }
        }

        SntchCard(title = "Session") {
            QuietButton("Sign out", { controller.signOut() })
            Spacer(Modifier.height(8.dp))
            QuietButton("Switch to student", onExitRole)
        }
    }

    if (confirmAddress) {
        ConfirmDialog(
            title = "Change controller address?",
            message = "You will be signed out and will need the teacher PIN again.",
            confirmText = "Change",
            onConfirm = {
                controller.applyAddress(addressDraft)
                confirmAddress = false
            },
            onDismiss = { confirmAddress = false },
        )
    }
}

// ------------------------------------------------------------------- login --

@Composable
private fun TeacherSignIn(controller: TeacherController, onExitRole: () -> Unit) {
    val scope = rememberCoroutineScope()
    var pin by remember { mutableStateOf("") }
    var addressDraft by remember { mutableStateOf(controller.address) }
    var probe by remember { mutableStateOf<DeviceInfo?>(null) }
    var probing by remember { mutableStateOf(false) }
    var probeError by remember { mutableStateOf<String?>(null) }

    Box(
        Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            Spacer(Modifier.height(20.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    Modifier
                        .size(44.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(Forest.primaryButton),
                    contentAlignment = Alignment.Center,
                ) { Text("S", color = Color(0xFF052E16), fontWeight = FontWeight.Black) }
                Spacer(Modifier.width(10.dp))
                Column {
                    Text("SNTCH", style = MaterialTheme.typography.headlineSmall)
                    Text(
                        "Teacher console",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }

            SntchCard(title = "Connect to the controller") {
                Field(addressDraft, { addressDraft = it }, "Controller address", keyboardType = KeyboardType.Uri)
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    QuietButton(if (probing) "Checking…" else "Test connection", {
                        probing = true
                        probeError = null
                        val candidate = addressDraft.trim()
                        controller.address = candidate
                        scope.launch {
                            try {
                                probe = probeController { com.sntch.app.data.SntchApi(candidate) }
                            } catch (failure: Throwable) {
                                probe = null
                                probeError = describeFailure(failure)
                            } finally {
                                probing = false
                            }
                        }
                    })
                }
                val found = probe
                if (found != null) {
                    InfoBanner(
                        "Found ${found.name.ifEmpty { "a controller" }} · ${found.firmware}",
                        Tone.OK,
                    )
                }
                val failed = probeError
                if (failed != null) {
                    InfoBanner(failed, Tone.BAD)
                }
            }

            SntchCard(title = "Teacher sign-in") {
                Field(pin, { pin = it.filter(Char::isDigit) }, "Teacher PIN", keyboardType = KeyboardType.NumberPassword)
                Text(
                    "The PIN is set when the firmware is flashed. It is never stored on this phone.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                controller.error?.let { InfoBanner(it, Tone.BAD) }
                PrimaryButton(
                    "Sign in",
                    {
                        controller.applyAddress(addressDraft)
                        controller.signIn(pin)
                    },
                    enabled = pin.isNotBlank() && addressDraft.isNotBlank(),
                    busy = controller.busy,
                )
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                QuietButton("Continue as student", onExitRole)
            }
            Text(
                "Offline-first: the controller creates its own Wi-Fi network. No internet connection is used.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

package com.sntch.app.ui

import androidx.compose.foundation.BorderStroke
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
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Star
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sntch.app.data.ApiException
import com.sntch.app.data.AttemptRef
import com.sntch.app.data.ExamAttempt
import com.sntch.app.data.LocalStore
import com.sntch.app.data.Prefs
import com.sntch.app.data.Question
import com.sntch.app.data.SessionPreview
import com.sntch.app.data.StudentRepository
import com.sntch.app.data.StudentState
import com.sntch.app.data.StudentWork
import com.sntch.app.data.Submission
import com.sntch.app.data.SubmissionDetail
import com.sntch.app.data.describeFailure
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

sealed interface StudentScreen {
    data object Home : StudentScreen
    data object Join : StudentScreen
    data class Work(val kind: String, val id: Int) : StudentScreen
    data class Exam(val examId: Int) : StudentScreen
    data object Results : StudentScreen
    data object Profile : StudentScreen
}

class StudentController(
    private val repo: StudentRepository,
    private val prefs: LocalStore,
    private val scope: CoroutineScope,
) {
    var screen by mutableStateOf<StudentScreen>(
        if (repo.joined) StudentScreen.Home else StudentScreen.Join,
    )
    var state by mutableStateOf<StudentState?>(null)
    var joined by mutableStateOf(repo.joined)
    var busy by mutableStateOf(false)
    var error by mutableStateOf<String?>(null)
    var notice by mutableStateOf<String?>(null)
    var preview by mutableStateOf<SessionPreview?>(null)
    var attempt by mutableStateOf<ExamAttempt?>(null)
    var attemptError by mutableStateOf<String?>(null)
    var detail by mutableStateOf<SubmissionDetail?>(null)
    var detailLoading by mutableStateOf(false)

    private var autosave: Job? = null
    private var refreshing = false
    private var submitting = false

    fun refresh(silent: Boolean = false) {
        if (!joined || refreshing) return
        refreshing = true
        scope.launch {
            try {
                state = repo.state()
            } catch (failure: Throwable) {
                error = describeFailure(failure)
                if (failure is ApiException && failure.status == 401) {
                    joined = false
                    state = null
                }
            } finally {
                refreshing = false
            }
            if (!silent) Unit
        }
    }

    fun checkCode(code: String) {
        if (busy) return
        busy = true
        error = null
        scope.launch {
            try {
                preview = repo.preview(code)
            } catch (failure: Throwable) {
                preview = null
                error = describeFailure(failure)
            } finally {
                busy = false
            }
        }
    }

    fun join(code: String, number: String, onJoined: () -> Unit) {
        if (busy) return
        busy = true
        error = null
        scope.launch {
            try {
                state = repo.join(code, number)
                joined = true
                preview = null
                screen = StudentScreen.Home
                notice = "Joined ${state?.session?.code ?: "the session"}."
                onJoined()
            } catch (failure: Throwable) {
                error = describeFailure(failure)
            } finally {
                busy = false
            }
        }
    }

    fun leave() {
        if (busy) return
        busy = true
        scope.launch {
            try {
                repo.leave()
            } catch (failure: Throwable) {
                // Leaving locally still matters if the controller is unreachable.
            } finally {
                joined = false
                state = null
                attempt = null
                screen = StudentScreen.Join
                notice = "You left the session."
                busy = false
            }
        }
    }

    fun startExam(examId: Int) {
        if (busy) return
        busy = true
        attemptError = null
        scope.launch {
            try {
                val started = repo.startExam(examId)
                val draft = prefs.examDraft(examId)
                if (draft.isNotEmpty()) {
                    started.answers.putAll(draft)
                }
                attempt = started
            } catch (failure: Throwable) {
                attemptError = describeFailure(failure)
            } finally {
                busy = false
            }
        }
    }

    fun answer(question: Question, value: Any) {
        val current = attempt ?: return
        current.answers[question.id.toString()] = value
        attempt = current.copy(answers = LinkedHashMap(current.answers))
        prefs.saveExamDraft(current.examId, current.answers)
        autosave?.cancel()
        autosave = scope.launch {
            delay(500)
            try {
                repo.saveAnswers(current.examId, current.answers)
                attemptError = null
            } catch (failure: Throwable) {
                // Autosave is best-effort; the draft stays on the phone.
                attemptError = "Answers kept on this phone. ${describeFailure(failure)}"
            }
        }
    }

    fun submitExam(auto: Boolean = false) {
        val current = attempt ?: return
        if (submitting) return
        submitting = true
        busy = true
        scope.launch {
            try {
                autosave?.cancel()
                val submission = try {
                    repo.submitExam(current.examId, current.answers)
                } catch (failure: ApiException) {
                    if (failure.status == 409) {
                        // Session paused/ended: try the autosaved path once.
                        repo.submitExam(current.examId, emptyMap())
                    } else {
                        throw failure
                    }
                }
                prefs.clearExamDraft(current.examId)
                attempt = null
                attemptError = null
                notice = if (auto) "Time is up. Answers submitted."
                else "Examination submitted: ${submission.score}/${submission.total}."
                screen = StudentScreen.Results
                state = repo.state()
            } catch (failure: Throwable) {
                attemptError = describeFailure(failure)
            } finally {
                submitting = false
                busy = false
            }
        }
    }

    fun tickClock() {
        val current = attempt ?: return
        if (current.remainingSec <= 1) {
            submitExam(auto = true)
        } else {
            attempt = current.copy(remainingSec = current.remainingSec - 1)
        }
    }

    fun submitActivity(work: StudentWork, text: String) {
        if (busy || text.trim().length < 10) return
        busy = true
        scope.launch {
            try {
                repo.submitActivity(work.id, text)
                prefs.saveActivityDraft(work.id, "")
                notice = "Response submitted."
                error = null
                state = repo.state()
            } catch (failure: Throwable) {
                error = describeFailure(failure)
            } finally {
                busy = false
            }
        }
    }

    fun loadDetail(id: Int) {
        detailLoading = true
        scope.launch {
            try {
                detail = repo.ownSubmission(id)
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

    fun applyAddress(value: String) {
        prefs.baseUrl = value
        repo.forget()
        joined = false
        state = null
        screen = StudentScreen.Join
    }

    fun sessionState(): String? = state?.session?.state

    fun currentAddress(): String = prefs.baseUrl

    fun activityDraft(id: Int): String = prefs.activityDraft(id)

    fun saveActivityDraft(id: Int, text: String) {
        prefs.saveActivityDraft(id, text)
    }
}

@Composable
fun StudentApp(
    repo: StudentRepository,
    prefs: LocalStore,
    onExitRole: () -> Unit,
) {
    val scope = rememberCoroutineScope()
    val controller = remember { StudentController(repo, prefs, scope) }

    LaunchedEffect(controller.joined) {
        while (controller.joined) {
            controller.refresh(silent = true)
            delay(3000)
        }
    }

    if (!controller.joined) {
        JoinScreen(controller, onExitRole)
        return
    }

    Scaffold(
        containerColor = MaterialTheme.colorScheme.background,
        bottomBar = {
            NavigationBar(containerColor = MaterialTheme.colorScheme.surface) {
                NavigationBarItem(
                    selected = controller.screen == StudentScreen.Home,
                    onClick = { controller.screen = StudentScreen.Home },
                    icon = { Icon(Icons.Filled.Home, contentDescription = null) },
                    label = { Text("Home") },
                )
                NavigationBarItem(
                    selected = controller.screen == StudentScreen.Results,
                    onClick = { controller.screen = StudentScreen.Results },
                    icon = { Icon(Icons.Filled.Star, contentDescription = null) },
                    label = { Text("Results") },
                )
                NavigationBarItem(
                    selected = controller.screen == StudentScreen.Profile,
                    onClick = { controller.screen = StudentScreen.Profile },
                    icon = { Icon(Icons.Filled.Person, contentDescription = null) },
                    label = { Text("Profile") },
                )
            }
        },
    ) { padding ->
        Box(Modifier.padding(padding).fillMaxSize()) {
            when (val screen = controller.screen) {
                StudentScreen.Home -> StudentHome(controller)
                StudentScreen.Results -> StudentResults(controller)
                StudentScreen.Profile -> StudentProfile(controller, onExitRole)
                StudentScreen.Join -> JoinScreen(controller, onExitRole)
                is StudentScreen.Work -> WorkScreen(controller, screen)
                is StudentScreen.Exam -> ExamScreen(controller, screen.examId)
            }
        }
    }

    controller.detail?.let { detail -> StudentDetailDialog(detail, controller) }
}

// -------------------------------------------------------------------- join --

@Composable
private fun JoinScreen(controller: StudentController, onExitRole: () -> Unit) {
    val scope = rememberCoroutineScope()
    var code by remember { mutableStateOf("") }
    var number by remember { mutableStateOf(controller.state?.me?.number ?: "") }
    var address by remember { mutableStateOf(controller.currentAddress()) }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier
                    .size(42.dp)
                    .clip(RoundedCornerShape(12.dp))
                    .background(Forest.primaryButton),
                contentAlignment = Alignment.Center,
            ) { Text("S", color = Color(0xFF052E16), fontWeight = FontWeight.Black) }
            Spacer(Modifier.width(10.dp))
            Column {
                Text("SNTCH", style = MaterialTheme.typography.headlineSmall)
                Text(
                    "Student",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Spacer(Modifier.weight(1f))
            QuietButton("Teacher", onClick = onExitRole)
        }

        controller.error?.let { InfoBanner(it, Tone.BAD) }
        controller.notice?.let { InfoBanner(it, Tone.OK) }

        SntchCard(title = "Join a session") {
            Field(
                code,
                { code = it.uppercase().take(8) },
                "Session code",
                placeholder = "SNT-ABCD",
                supporting = "The code is on the board or in your teacher's session screen.",
            )
            Field(
                number,
                { number = it },
                "Student number",
                placeholder = "001",
                keyboardType = KeyboardType.Text,
            )
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                QuietButton("Check code", onClick = {
                    controller.checkCode(code)
                    // Keep the typed number; preview only announces the session.
                }, enabled = code.length >= 8 && !controller.busy)
                PrimaryButton(
                    "Join session",
                    onClick = {
                        controller.applyAddress(address)
                        controller.join(code, number) {}
                    },
                    enabled = code.length >= 8 && number.isNotBlank() && !controller.busy,
                    busy = controller.busy,
                )
            }
            val preview = controller.preview
            if (preview != null) {
                Surface(
                    shape = RoundedCornerShape(12.dp),
                    color = Forest.okBg,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Column(Modifier.padding(12.dp)) {
                        Text(preview.title, style = MaterialTheme.typography.titleMedium, color = Forest.okFg)
                        Text(
                            "${preview.refType.replaceFirstChar { it.uppercase() }} · section ${preview.target} · " +
                                statusLabel(preview.state),
                            style = MaterialTheme.typography.bodyMedium,
                            color = Forest.okFg,
                        )
                    }
                }
            }
        }

        SntchCard(title = "Connection") {
            Field(address, { address = it }, "Controller address", keyboardType = KeyboardType.Uri)
            Text(
                "1. Connect this phone to the SNTCH-CLASS Wi-Fi network.\n" +
                    "2. Keep mobile data off if your phone prefers the internet.\n" +
                    "3. Join with the code your teacher shows.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            QuietButton("Use this address", onClick = { controller.applyAddress(address) })
        }

        Text(
            "No internet is needed. Everything is stored on the classroom controller.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        QuietButton("Switch to teacher", onClick = onExitRole)
    }
}

// -------------------------------------------------------------------- home --

@Composable
private fun StudentHome(controller: StudentController) {
    val state = controller.state
    if (state == null) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                CircularProgressIndicator(color = Forest.emerald)
                Spacer(Modifier.height(10.dp))
                Text(controller.error ?: "Loading…", style = MaterialTheme.typography.bodyMedium)
                Spacer(Modifier.height(10.dp))
                QuietButton("Retry", onClick = { controller.refresh() })
            }
        }
        return
    }

    val session = state.session
    val work = state.availableWork.filter { !it.submitted }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    "${state.me.section} · ${state.me.number}",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Text("Hi, ${state.me.name}", style = MaterialTheme.typography.headlineSmall)
            }
            ConnectionChip(controller.error == null, if (controller.error == null) "Connected" else "Offline")
        }

        controller.error?.let { InfoBanner("Reconnecting… $it", Tone.BAD) }
        controller.notice?.let { InfoBanner(it, Tone.OK) }

        if (session == null) {
            SntchCard {
                EmptyState(
                    "Not in a session",
                    "Join with the code your teacher shows on the board.",
                    "Enter code",
                ) { controller.screen = StudentScreen.Join }
            }
        } else {
            SntchCard(title = "Current session") {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    CodePill(session.code, big = true)
                    Spacer(Modifier.weight(1f))
                    Badge(statusLabel(session.state), statusTone(session.state))
                }
                Spacer(Modifier.height(8.dp))
                Text(session.title, style = MaterialTheme.typography.titleMedium)
                Text(
                    when (session.state) {
                        "live" -> "Open now — you can work."
                        "paused" -> "Paused by your teacher. Your work is kept."
                        "ready" -> "Waiting for your teacher to start."
                        else -> "This session has ended."
                    },
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                if (session.state != "ended" && session.state != "expired") {
                    Text(
                        "Expires in ${session.expiresInSec / 60} min",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }

        val attempt = controller.attempt
        if (attempt != null) {
            SntchCard(title = "Examination in progress") {
                Text(
                    "${attempt.title} · ${clockText(attempt.remainingSec)} left",
                    style = MaterialTheme.typography.bodyLarge,
                )
                Spacer(Modifier.height(8.dp))
                PrimaryButton("Resume exam", onClick = {
                    controller.screen = StudentScreen.Exam(attempt.examId)
                })
            }
        }

        SectionHeading("Open work")
        if (work.isEmpty()) {
            SntchCard {
                EmptyState("Nothing open right now", "Your teacher will open activities and exams for your section.")
            }
        } else {
            work.forEach { item ->
                WorkCard(item) {
                    controller.screen = if (item.isExam) StudentScreen.Exam(item.id)
                    else StudentScreen.Work(item.kind, item.id)
                }
            }
        }

        val past = state.availableWork.filter { it.submitted }
        if (past.isNotEmpty()) {
            SectionHeading("Submitted")
            past.forEach { item ->
                SntchCard {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Column(Modifier.weight(1f)) {
                            Text(item.title, style = MaterialTheme.typography.bodyLarge)
                            Text(
                                if (item.isExam) "Examination" else "Activity",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        Badge("Submitted", Tone.OK)
                    }
                }
            }
        }

        val recent = state.mySubmissions.takeLast(3).reversed()
        if (recent.isNotEmpty()) {
            SectionHeading("Recent results", "All results →")
            recent.forEach { submission ->
                SubmissionTile(submission) { controller.loadDetail(submission.id) }
            }
        }

        QuietButton("Leave session", onClick = { controller.leave() })
    }
}

@Composable
private fun WorkCard(work: StudentWork, onOpen: () -> Unit) {
    Surface(
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline),
        modifier = Modifier.fillMaxWidth().clickable { onOpen() },
    ) {
        Row(Modifier.padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    if (work.isExam) "EXAMINATION" else "ACTIVITY",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary,
                )
                Text(work.title, style = MaterialTheme.typography.titleMedium)
                Text(
                    if (work.isExam) "${work.questionCount} questions · ${work.durationMin} min · pass ${work.passingPct}%"
                    else "Response activity",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            QuietButton(if (work.isExam) "Open" else "Answer", onClick = onOpen)
        }
    }
}

// ----------------------------------------------------------------- activity --

@Composable
private fun WorkScreen(controller: StudentController, screen: StudentScreen.Work) {
    val state = controller.state
    val work = state?.availableWork?.firstOrNull { it.kind == screen.kind && it.id == screen.id }
    val session = state?.session
    val submission = state?.mySubmissions?.firstOrNull { it.refType == "activity" && it.refId == screen.id }
    var text by remember(screen.id) { mutableStateOf(controller.activityDraft(screen.id)) }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            QuietButton("Back", onClick = { controller.screen = StudentScreen.Home })
            Spacer(Modifier.weight(1f))
            work?.let { Badge(statusLabel(it.status), statusTone(it.status)) }
        }
        if (work == null) {
            SntchCard { EmptyState("Activity unavailable", "It may have closed while you were away.") }
            return@Column
        }
        Text(work.title, style = MaterialTheme.typography.headlineSmall)
        Text(work.instructions.ifEmpty { "Answer in your own words." }, style = MaterialTheme.typography.bodyLarge)

        when {
            submission != null -> {
                InfoBanner(
                    if (submission.status == "reviewed") "Your teacher reviewed this response."
                    else "Submitted. Waiting for your teacher to review it.",
                    if (submission.status == "reviewed") Tone.OK else Tone.INFO,
                )
                SntchCard(title = "Your response") {
                    Text(
                        "Your response is stored on the controller. Open it to read what you submitted.",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Spacer(Modifier.height(8.dp))
                    QuietButton("Open submission", onClick = { controller.loadDetail(submission.id) })
                }
            }
            session == null -> InfoBanner("Join the session again to submit.", Tone.WARN)
            session.state == "paused" -> InfoBanner("Paused by your teacher. You can submit once it resumes.", Tone.WARN)
            session.state != "live" -> InfoBanner("Waiting for your teacher to start the session.", Tone.WARN)
            else -> {
                SntchCard(title = "Your response") {
                    Field(
                        text,
                        {
                            text = it.take(500)
                            controller.saveActivityDraft(screen.id, text)
                        },
                        "Response",
                        singleLine = false,
                        minLines = 6,
                        maxLines = 10,
                        supporting = "${text.length}/500 characters · at least 10",
                    )
                    controller.error?.let { InfoBanner(it, Tone.BAD) }
                    PrimaryButton(
                        "Submit response",
                        onClick = { controller.submitActivity(work, text) },
                        enabled = text.trim().length >= 10,
                        busy = controller.busy,
                    )
                }
            }
        }
    }
}

// --------------------------------------------------------------------- exam --

@Composable
private fun ExamScreen(controller: StudentController, examId: Int) {
    val state = controller.state
    val work = state?.availableWork?.firstOrNull { it.id == examId && it.kind == "exam" }
    val session = state?.session
    val submission = state?.mySubmissions?.firstOrNull { it.refType == "exam" && it.refId == examId }
    val attempt = controller.attempt
    val live = session?.state == "live" && session.refId == examId

    // Coming back to the exam screen (or after a reconnect) resyncs the clock,
    // questions and autosaved answers from the controller.
    LaunchedEffect(examId, state?.activeAttempt?.examId) {
        if (controller.attempt == null && state?.activeAttempt?.examId == examId) {
            controller.startExam(examId)
        }
    }

    LaunchedEffect(attempt?.examId, session?.state) {
        while (attempt != null) {
            delay(1000)
            if (controller.attempt == null) break
            if (controller.sessionState() == "live") controller.tickClock()
        }
    }

    if (attempt != null) {
        val current = controller.attempt
        if (current != null) {
            ExamTaking(controller, current, live)
            return
        }
    }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        QuietButton("Back", onClick = { controller.screen = StudentScreen.Home })
        if (work == null) {
            SntchCard { EmptyState("Examination unavailable", "It may have closed while you were away.") }
            return@Column
        }
        controller.notice?.let { InfoBanner(it, Tone.OK) }
        controller.error?.let { InfoBanner(it, Tone.BAD) }
        controller.attemptError?.let { InfoBanner(it, Tone.BAD) }

        if (submission != null) {
            SntchCard(title = "Result") {
                Text(
                    "${submission.score}/${submission.total} · ${submission.percent}%",
                    style = MaterialTheme.typography.headlineSmall,
                    fontFamily = FontFamily.Monospace,
                )
                Badge(
                    if (submission.passed) "Passed" else "Failed",
                    if (submission.passed) Tone.OK else Tone.BAD,
                )
                if (submission.timeout) {
                    InfoBanner("Submitted automatically when time ran out or the session ended.", Tone.WARN)
                }
                Spacer(Modifier.height(8.dp))
                QuietButton("Review answers", onClick = { controller.loadDetail(submission.id) })
            }
            return@Column
        }

        SntchCard {
            Text(work.title, style = MaterialTheme.typography.headlineSmall)
            Text(
                work.instructions.ifEmpty { "Answer every question, then submit." },
                style = MaterialTheme.typography.bodyLarge,
            )
            Spacer(Modifier.height(10.dp))
            Text(
                "${work.questionCount} questions · ${work.durationMin} minutes · pass mark ${work.passingPct}%",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(Modifier.height(10.dp))
            when {
                session == null -> InfoBanner("Join this examination's session first.", Tone.WARN)
                session.refId != examId -> InfoBanner("You are in a different session.", Tone.WARN)
                session.state == "paused" -> InfoBanner("Paused by your teacher. The timer is stopped.", Tone.WARN)
                session.state != "live" -> InfoBanner("Waiting for your teacher to start the session.", Tone.WARN)
                else -> InfoBanner("The timer starts as soon as you tap Start. It is kept by the controller.", Tone.INFO)
            }
            Spacer(Modifier.height(10.dp))
            PrimaryButton(
                "Start examination",
                onClick = { controller.startExam(examId) },
                enabled = live && !controller.busy,
                busy = controller.busy,
            )
        }
    }
}

@Composable
private fun ExamTaking(controller: StudentController, attempt: ExamAttempt, live: Boolean) {
    var index by remember(attempt.examId) { mutableStateOf(0) }
    var reviewing by remember(attempt.examId) { mutableStateOf(false) }
    var confirmSubmit by remember { mutableStateOf(false) }
    val questions = attempt.questions
    if (questions.isEmpty()) {
        SntchCard { EmptyState("No questions", "Ask your teacher — this examination has no questions.") }
        return
    }
    val question = questions[index.coerceIn(0, questions.size - 1)]
    val answered = questions.count { question ->
        val value = attempt.answers[question.id.toString()]
        when (value) {
            null -> false
            is String -> value.isNotBlank()
            else -> true
        }
    }
    val unanswered = questions.size - answered

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(attempt.title, style = MaterialTheme.typography.titleMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(
                    if (reviewing) "Review before submitting" else "Question ${index + 1} of ${questions.size}",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Text(
                clockText(attempt.remainingSec),
                style = MaterialTheme.typography.headlineSmall,
                fontFamily = FontFamily.Monospace,
                color = if (attempt.remainingSec < 60) Forest.badFg else MaterialTheme.colorScheme.onSurface,
            )
        }

        if (!live) InfoBanner("Your teacher paused the session. The timer is frozen.", Tone.WARN)
        controller.attemptError?.let { InfoBanner(it, Tone.WARN) }
        ProgressBar((index + 1).toFloat() / questions.size)
        Text(
            "$answered of ${questions.size} answered · saved on the controller",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        if (!reviewing) {
            SntchCard {
                Text(
                    if (question.isMultipleChoice) "Choose one answer" else "Short answer",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary,
                )
                Text(question.prompt, style = MaterialTheme.typography.titleLarge)
                Spacer(Modifier.height(8.dp))
                if (question.isMultipleChoice) {
                    question.options.forEachIndexed { optionIndex, option ->
                        val selected = attempt.answers[question.id.toString()] == optionIndex
                        Surface(
                            shape = RoundedCornerShape(12.dp),
                            color = if (selected) Forest.okBg else MaterialTheme.colorScheme.surfaceVariant,
                            border = BorderStroke(
                                1.dp,
                                if (selected) Forest.okFg else MaterialTheme.colorScheme.outline,
                            ),
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { controller.answer(question, optionIndex) },
                        ) {
                            Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                                Text(
                                    ('A' + optionIndex).toString(),
                                    fontWeight = FontWeight.Bold,
                                    modifier = Modifier.width(24.dp),
                                )
                                Text(option, modifier = Modifier.weight(1f))
                                if (selected) Icon(Icons.Filled.Check, contentDescription = null, tint = Forest.okFg)
                            }
                        }
                    }
                } else {
                    var text by remember(question.id) {
                        mutableStateOf(attempt.answers[question.id.toString()]?.toString() ?: "")
                    }
                    Field(
                        text,
                        {
                            text = it.take(200)
                            controller.answer(question, text)
                        },
                        "Your answer",
                        singleLine = false,
                        minLines = 2,
                        supporting = "${text.length}/200 characters",
                    )
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                QuietButton("Previous", onClick = { index = (index - 1).coerceAtLeast(0) }, enabled = index > 0)
                Spacer(Modifier.weight(1f))
                if (index < questions.size - 1) {
                    PrimaryButton("Next", onClick = { index += 1 })
                } else {
                    PrimaryButton("Review answers", onClick = { reviewing = true })
                }
            }
        } else {
            if (unanswered > 0) {
                InfoBanner("$unanswered question(s) still unanswered. Answer all to submit.", Tone.WARN)
            } else {
                InfoBanner("All questions answered. You can submit.", Tone.OK)
            }
            questions.forEachIndexed { questionIndex, item ->
                val raw = attempt.answers[item.id.toString()]
                val shown = when {
                    raw == null || (raw is String && raw.isBlank()) -> "No answer"
                    item.isMultipleChoice -> item.options.getOrNull((raw as? Int) ?: -1) ?: "—"
                    else -> raw.toString()
                }
                SntchCard {
                    Text("Question ${questionIndex + 1}", style = MaterialTheme.typography.labelSmall)
                    Text(item.prompt, style = MaterialTheme.typography.bodyLarge)
                    Text(shown, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(Modifier.height(6.dp))
                    QuietButton("Edit", onClick = {
                        index = questionIndex
                        reviewing = false
                    })
                }
            }
            PrimaryButton(
                "Submit examination",
                onClick = { confirmSubmit = true },
                enabled = unanswered == 0 && !controller.busy,
                busy = controller.busy,
            )
            QuietButton("Back to questions", onClick = { reviewing = false })
        }
    }

    if (confirmSubmit) {
        ConfirmDialog(
            title = "Submit examination?",
            message = "You cannot change your answers after submitting.",
            confirmText = "Submit",
            onConfirm = {
                confirmSubmit = false
                controller.submitExam()
            },
            onDismiss = { confirmSubmit = false },
        )
    }
}

// ------------------------------------------------------------------ results --

@Composable
private fun StudentResults(controller: StudentController) {
    val state = controller.state
    val mine = state?.mySubmissions.orEmpty().sortedByDescending { it.id }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Results", style = MaterialTheme.typography.headlineSmall)
        controller.error?.let { InfoBanner(it, Tone.BAD) }
        controller.notice?.let { InfoBanner(it, Tone.OK) }
        if (mine.isEmpty()) {
            SntchCard {
                EmptyState("No results yet", "Submitted activities and examinations appear here.")
            }
        } else {
            val exams = mine.filter { it.isExam }
            val average = if (exams.isEmpty()) null else exams.sumOf { it.percent } / exams.size
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                StatTile("Exams taken", exams.size.toString(), null, Modifier.weight(1f))
                StatTile("Average", average?.let { "$it%" } ?: "—", null, Modifier.weight(1f))
                StatTile("Passed", exams.count { it.passed }.toString(), null, Modifier.weight(1f))
            }
            mine.forEach { submission ->
                SubmissionTile(submission) { controller.loadDetail(submission.id) }
            }
        }
    }
}

@Composable
private fun SubmissionTile(submission: Submission, onClick: () -> Unit) {
    Surface(
        shape = RoundedCornerShape(14.dp),
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outline),
        modifier = Modifier.fillMaxWidth().clickable { onClick() },
    ) {
        Row(Modifier.padding(14.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    if (submission.isExam) "EXAMINATION" else "ACTIVITY",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.primary,
                )
                Text(
                    submission.title.ifEmpty { "Submission ${submission.id}" },
                    style = MaterialTheme.typography.titleMedium,
                )
                Text(
                    if (submission.isExam) "${submission.score}/${submission.total} · pass ${if (submission.passed) "yes" else "no"}"
                    else "Waiting for review",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            if (submission.isExam) {
                Column(horizontalAlignment = Alignment.End) {
                    Text(
                        "${submission.percent}%",
                        style = MaterialTheme.typography.titleLarge,
                        fontFamily = FontFamily.Monospace,
                    )
                    Badge(if (submission.passed) "Passed" else "Failed", if (submission.passed) Tone.OK else Tone.BAD)
                }
            } else {
                Badge(statusLabel(submission.status), statusTone(submission.status))
            }
        }
    }
}

@Composable
private fun StudentDetailDialog(detail: SubmissionDetail, controller: StudentController) {
    SntchDialog(
        title = detail.summary.title.ifEmpty { "Result" },
        onDismiss = { controller.closeDetail() },
        dismissText = "Close",
    ) {
        if (detail.summary.isExam) {
            Text(
                "${detail.summary.score}/${detail.summary.total} · ${detail.summary.percent}%",
                style = MaterialTheme.typography.headlineSmall,
                fontFamily = FontFamily.Monospace,
            )
            Badge(
                if (detail.summary.passed) "Passed" else "Failed",
                if (detail.summary.passed) Tone.OK else Tone.BAD,
            )
            if (!detail.answersVisible) {
                InfoBanner("Correct answers are hidden by your teacher's settings.", Tone.INFO)
            } else {
                detail.answers.forEach { (questionId, answer) ->
                    val question = detail.questionFor(questionId)
                    Text(
                        question?.prompt ?: "Question $questionId",
                        style = MaterialTheme.typography.bodyLarge,
                    )
                    val shown = if (answer.isBlank()) {
                        "No answer"
                    } else if (question != null && question.isMultipleChoice) {
                        val index = answer.toIntOrNull() ?: -1
                        val option = question.options.getOrNull(index) ?: answer
                        if (question.correct >= 0 && index == question.correct) "$option (correct)" else option
                    } else {
                        answer
                    }
                    Text(
                        shown,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        } else {
            Badge(statusLabel(detail.summary.status), statusTone(detail.summary.status))
            Text(detail.text.ifEmpty { "(empty response)" }, style = MaterialTheme.typography.bodyLarge)
        }
    }
}

// ------------------------------------------------------------------ profile --

@Composable
private fun StudentProfile(controller: StudentController, onExitRole: () -> Unit) {
    val state = controller.state
    var address by remember { mutableStateOf(controller.currentAddress()) }

    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text("Profile", style = MaterialTheme.typography.headlineSmall)
        SntchCard {
            Text(state?.me?.name ?: "Student", style = MaterialTheme.typography.titleLarge)
            Text(
                "${state?.me?.section ?: "—"} · student number ${state?.me?.number ?: "—"}",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        SntchCard(title = "Session") {
            val session = state?.session
            if (session == null) {
                Text("Not in a session.", style = MaterialTheme.typography.bodyMedium)
                QuietButton("Join a session", onClick = { controller.screen = StudentScreen.Join })
            } else {
                Text("${session.title} (${session.code})", style = MaterialTheme.typography.bodyLarge)
                Text(
                    "Section ${session.target} · ${statusLabel(session.state)}",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                Spacer(Modifier.height(8.dp))
                QuietButton("Leave session", onClick = { controller.leave() })
            }
        }
        SntchCard(title = "Connection") {
            Field(address, { address = it }, "Controller address", keyboardType = KeyboardType.Uri)
            Text(
                "The controller is the classroom ESP32-S3 access point. No internet is used.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            QuietButton("Apply address", onClick = { controller.applyAddress(address) })
        }
        SntchCard(title = "App") {
            QuietButton("Switch to teacher", onClick = onExitRole)
        }
        Text(
            "Work is stored on the controller. This phone keeps only your unsent drafts.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

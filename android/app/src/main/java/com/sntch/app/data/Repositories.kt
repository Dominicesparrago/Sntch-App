package com.sntch.app.data

import android.content.Context
import java.io.File
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject

/**
 * Everything the app remembers locally. An interface so repositories can be
 * exercised on the JVM with a fake store (SharedPreferences needs Android).
 */
interface LocalStore {
    var baseUrl: String
    var role: String?
    var teacherToken: String?
    var studentToken: String?
    var studentName: String
    var studentNumber: String
    var studentSection: String
    var showCorrect: Boolean
    fun activityDraft(id: Int): String
    fun saveActivityDraft(id: Int, text: String)
    fun examDraft(examId: Int): Map<String, Any>
    fun saveExamDraft(examId: Int, answers: Map<String, Any>)
    fun clearExamDraft(examId: Int)
    fun saveDeletionBackup(sessionId: Int, json: String): Boolean
    fun deletionBackup(sessionId: Int): String?
    fun clearDeletionBackup(sessionId: Int)
    fun clearStudentSession()
    fun forgetTeacher()
}

/** Local app memory: controller address, role, tokens, drafts. Never stores the teacher PIN. */
class Prefs(private val context: Context) : LocalStore {
    private val store = context.getSharedPreferences("sntch.v1", Context.MODE_PRIVATE)
    private val backupDir: File get() = File(context.filesDir, "session-backups")

    private fun backupFile(sessionId: Int): File = File(backupDir, "$sessionId.json")

    override var baseUrl: String
        get() = store.getString("baseUrl", SntchApi.DEFAULT_ADDRESS) ?: SntchApi.DEFAULT_ADDRESS
        set(value) {
            store.edit().putString("baseUrl", value.trim().ifEmpty { SntchApi.DEFAULT_ADDRESS }).apply()
        }

    override var role: String?
        get() = store.getString("role", null)
        set(value) {
            store.edit().putString("role", value).apply()
        }

    override var teacherToken: String?
        get() = store.getString("teacherToken", null)
        set(value) {
            store.edit().putString("teacherToken", value).apply()
        }

    override var studentToken: String?
        get() = store.getString("studentToken", null)
        set(value) {
            store.edit().putString("studentToken", value).apply()
        }

    override var studentName: String
        get() = store.getString("studentName", "") ?: ""
        set(value) {
            store.edit().putString("studentName", value).apply()
        }

    override var studentNumber: String
        get() = store.getString("studentNumber", "") ?: ""
        set(value) {
            store.edit().putString("studentNumber", value).apply()
        }

    override var studentSection: String
        get() = store.getString("studentSection", "") ?: ""
        set(value) {
            store.edit().putString("studentSection", value).apply()
        }

    override var showCorrect: Boolean
        get() = store.getBoolean("showCorrect", true)
        set(value) {
            store.edit().putBoolean("showCorrect", value).apply()
        }

    override fun activityDraft(id: Int): String = store.getString("activity-$id", "") ?: ""
    override fun saveActivityDraft(id: Int, text: String) {
        store.edit().putString("activity-$id", text).apply()
    }

    override fun examDraft(examId: Int): Map<String, Any> {
        val raw = store.getString("exam-$examId", null) ?: return emptyMap()
        return try {
            val json = JSONObject(raw)
            json.keys().asSequence().associateWith { key ->
                val value = json.opt(key)
                if (value is Number) value.toInt() else value?.toString() ?: ""
            }
        } catch (error: Exception) {
            emptyMap()
        }
    }

    override fun saveExamDraft(examId: Int, answers: Map<String, Any>) {
        val json = JSONObject()
        answers.forEach { (key, value) -> json.put(key, value) }
        store.edit().putString("exam-$examId", json.toString()).apply()
    }

    override fun clearExamDraft(examId: Int) {
        store.edit().remove("exam-$examId").apply()
    }

    override fun saveDeletionBackup(sessionId: Int, json: String): Boolean = try {
        val dir = backupDir
        if (!dir.isDirectory && !dir.mkdirs()) {
            false
        } else {
            val target = backupFile(sessionId)
            val temp = File(dir, "$sessionId.json.tmp")
            temp.writeText(json, Charsets.UTF_8)
            try {
                Files.move(
                    temp.toPath(),
                    target.toPath(),
                    StandardCopyOption.REPLACE_EXISTING,
                    StandardCopyOption.ATOMIC_MOVE,
                )
            } catch (error: Exception) {
                Files.move(temp.toPath(), target.toPath(), StandardCopyOption.REPLACE_EXISTING)
            }
            target.isFile && target.readText(Charsets.UTF_8) == json
        }
    } catch (error: Exception) {
        false
    }

    override fun deletionBackup(sessionId: Int): String? = try {
        backupFile(sessionId).takeIf { it.isFile }?.readText(Charsets.UTF_8)
    } catch (error: Exception) {
        null
    }

    override fun clearDeletionBackup(sessionId: Int) {
        backupFile(sessionId).delete()
        File(backupDir, "$sessionId.json.tmp").delete()
    }

    override fun clearStudentSession() {
        store.edit().remove("studentToken").remove("studentSection").apply()
    }

    override fun forgetTeacher() {
        store.edit().remove("teacherToken").apply()
    }
}

private suspend fun <T> io(block: () -> T): T = withContext(Dispatchers.IO) { block() }

/** Connects to a controller and reports what it is, without authenticating. */
suspend fun probeController(apiProvider: () -> SntchApi): DeviceInfo = io {
    val json = apiProvider().health()
    DeviceInfo(
        name = json.str("deviceName"),
        ssid = json.str("ssid"),
        ip = json.str("ip"),
        firmware = json.str("firmware"),
        uptimeMs = json.long("uptimeMs"),
        freeHeap = json.int("freeHeap"),
        storageUsed = json.int("storageUsed"),
        storageTotal = json.int("storageTotal"),
        storageError = json.bool("storageError"),
        students = json.int("students"),
        openSessions = json.int("activeSessions"),
    )
}

data class NewQuestion(
    val type: String,
    val prompt: String,
    val options: List<String> = emptyList(),
    val correct: Int = -1,
    val keys: List<String> = emptyList(),
)

class TeacherRepository(
    private val apiProvider: () -> SntchApi,
    private val prefs: LocalStore,
) {
    var token: String? = prefs.teacherToken
        private set

    val signedIn: Boolean get() = token != null

    suspend fun login(pin: String): DeviceInfo = io {
        val response = apiProvider().post("/api/teacher/login", JSONObject().put("pin", pin))
        token = response.str("token").takeIf { it.isNotEmpty() }
        prefs.teacherToken = token
        DeviceInfo.parse(response.optJSONObject("device") ?: JSONObject())
    }

    suspend fun state(): TeacherState = io {
        val token = requireToken()
        try {
            TeacherState.parse(apiProvider().get("/api/teacher/state", token))
        } catch (error: ApiException) {
            if (error.status == 401) forget()
            throw error
        }
    }

    suspend fun importStudents(rows: List<Triple<String, String, String>>): Pair<Int, Int> = io {
        val students = JSONArray()
        rows.forEach { (number, name, section) ->
            students.put(
                JSONObject()
                    .put("number", number.trim())
                    .put("name", name.trim())
                    .put("section", section.trim()),
            )
        }
        val response = apiProvider().post(
            "/api/teacher/students",
            JSONObject().put("students", students),
            requireToken(),
        )
        response.int("added") to response.int("updated")
    }

    suspend fun createActivity(
        title: String,
        description: String,
        instructions: String,
        target: String,
    ): Int = io {
        val response = apiProvider().post(
            "/api/teacher/activities",
            JSONObject()
                .put("title", title.trim())
                .put("description", description.trim())
                .put("instructions", instructions.trim())
                .put("target", target.trim()),
            requireToken(),
        )
        response.int("id")
    }

    suspend fun createExam(
        title: String,
        instructions: String,
        target: String,
        durationMin: Int,
        passingPct: Int,
        questions: List<NewQuestion>,
    ): Int = io {
        val array = JSONArray()
        questions.forEach { question ->
            val json = JSONObject()
                .put("type", question.type)
                .put("prompt", question.prompt.trim())
            if (question.type == "mc") {
                val options = JSONArray()
                question.options.forEach { options.put(it.trim()) }
                json.put("options", options).put("correct", question.correct)
            } else {
                val keys = JSONArray()
                question.keys.forEach { keys.put(it.trim()) }
                json.put("keys", keys)
            }
            array.put(json)
        }
        val response = apiProvider().post(
            "/api/teacher/exams",
            JSONObject()
                .put("title", title.trim())
                .put("instructions", instructions.trim())
                .put("target", target.trim())
                .put("durationMin", durationMin)
                .put("passingPct", passingPct)
                .put("questions", array),
            requireToken(),
        )
        response.int("id")
    }

    suspend fun publish(kind: String, id: Int) = io { post("/api/teacher/${kind}s/$id/publish") }

    suspend fun complete(kind: String, id: Int) = io { post("/api/teacher/${kind}s/$id/complete") }

    suspend fun createSession(kind: String, id: Int, minutes: Int): Int = io {
        val response = post(
            "/api/teacher/sessions",
            JSONObject()
                .put("refType", kind)
                .put("refId", id)
                .put("expiresInMin", minutes),
        )
        response.int("id")
    }

    suspend fun sessionAction(id: Int, action: String) = io {
        post("/api/teacher/sessions/$id/$action")
    }

    suspend fun deleteSession(id: Int): PendingDeletion = io {
        val response = post("/api/teacher/sessions/$id/delete")
        saveDeletionBackupAndAck(id)
        PendingDeletion.parse(response)
    }

    suspend fun finishDeletion(id: Int) = io {
        saveDeletionBackupAndAck(id)
    }

    suspend fun cancelDeletion(id: Int) = io {
        post("/api/teacher/deletions/$id/cancel")
        prefs.clearDeletionBackup(id)
    }

    suspend fun pendingDeletions(): List<PendingDeletion> = io {
        val response = apiProvider().get("/api/teacher/deletions", requireToken())
        response.array("pendingDeletions").objects().map { PendingDeletion.parse(it) }
    }

    suspend fun deletionBackup(id: Int): String? = io {
        prefs.deletionBackup(id)
    }

    suspend fun markAttendance(
        date: String,
        section: String,
        studentId: Int? = null,
        status: String? = null,
        markAll: String? = null,
    ) = io {
        val body = JSONObject().put("date", date).put("section", section)
        if (markAll != null) body.put("markAll", markAll)
        if (studentId != null) body.put("studentId", studentId)
        if (status != null) body.put("status", status)
        post("/api/teacher/attendance", body)
    }

    suspend fun review(submissionId: Int) = io {
        post("/api/teacher/submissions/$submissionId/review")
    }

    suspend fun setShowCorrect(show: Boolean) = io {
        post("/api/teacher/settings", JSONObject().put("showCorrect", show))
    }

    suspend fun submissionDetail(id: Int): SubmissionDetail = io {
        val json = apiProvider().get("/api/teacher/submissions/$id", requireToken())
        // A signed-in teacher may always see what was submitted.
        SubmissionDetail.parse(json).copy(answersVisible = true)
    }

    fun forget() {
        token = null
        prefs.forgetTeacher()
    }

    private fun saveDeletionBackupAndAck(id: Int) {
        val backup = apiProvider().get("/api/teacher/deletions/$id", requireToken())
        if (!prefs.saveDeletionBackup(id, backup.toString())) {
            throw ApiException(
                "BACKUP_NOT_SAVED",
                "Could not save the session backup on this device; the session was not deleted.",
                500,
            )
        }
        post("/api/teacher/deletions/$id/ack")
    }

    private fun requireToken(): String =
        token ?: throw ApiException("UNAUTHORIZED", "Sign in again.", 401)

    private fun post(path: String, body: JSONObject? = null): JSONObject =
        apiProvider().post(path, body, requireToken())
}

class StudentRepository(
    private val apiProvider: () -> SntchApi,
    private val prefs: LocalStore,
) {
    var token: String? = prefs.studentToken
        private set

    val joined: Boolean get() = token != null

    suspend fun preview(code: String): SessionPreview = io {
        val response = apiProvider().get("/api/session/${code.trim().uppercase()}")
        SessionPreview.parse(response.optJSONObject("session") ?: JSONObject())
    }

    suspend fun join(code: String, studentNumber: String): StudentState {
        val response = io {
            apiProvider().post(
                "/api/student/join",
                JSONObject()
                    .put("code", code.trim().uppercase())
                    .put("studentNumber", studentNumber.trim()),
            )
        }
        token = response.str("studentToken").takeIf { it.isNotEmpty() }
        prefs.studentToken = token
        val me = response.optJSONObject("student") ?: JSONObject()
        prefs.studentName = me.str("name")
        prefs.studentNumber = me.str("number")
        prefs.studentSection = me.str("section")
        return state()
    }

    suspend fun state(): StudentState = io {
        val token = requireToken()
        try {
            StudentState.parse(apiProvider().get("/api/student/state", token))
        } catch (error: ApiException) {
            if (error.status == 401) forget()
            throw error
        }
    }

    suspend fun startExam(examId: Int): ExamAttempt = io {
        val response = apiProvider().post("/api/student/exams/$examId/start", null, requireToken())
        val attempt = response.optJSONObject("attempt") ?: JSONObject()
        val answers = LinkedHashMap<String, Any>()
        attempt.optJSONObject("answers")?.let { saved ->
            saved.keys().asSequence().forEach { key ->
                val value = saved.opt(key)
                answers[key] = if (value is Number) value.toInt() else value?.toString() ?: ""
            }
        }
        ExamAttempt(
            examId = attempt.int("examId", examId),
            title = attempt.str("title"),
            durationMin = attempt.int("durationMin"),
            passingPct = attempt.int("passingPct"),
            remainingSec = attempt.int("remainingSec"),
            questions = attempt.array("questions").objects().map { Question.parse(it) },
            answers = answers,
        )
    }

    suspend fun saveAnswers(examId: Int, answers: Map<String, Any>) = io {
        val payload = JSONObject()
        answers.forEach { (key, value) -> payload.put(key, value) }
        apiProvider().post(
            "/api/student/attempts/$examId/answers",
            JSONObject().put("answers", payload),
            requireToken(),
        )
    }

    suspend fun submitActivity(refId: Int, text: String): Submission = io {
        val response = apiProvider().post(
            "/api/student/submit",
            JSONObject()
                .put("refType", "activity")
                .put("refId", refId)
                .put("text", text.trim()),
            requireToken(),
        )
        Submission.parse(response.optJSONObject("submission") ?: JSONObject())
    }

    suspend fun submitExam(examId: Int, answers: Map<String, Any>): Submission = io {
        val payload = JSONObject()
        answers.forEach { (key, value) -> payload.put(key, value) }
        val response = apiProvider().post(
            "/api/student/submit",
            JSONObject()
                .put("refType", "exam")
                .put("refId", examId)
                .put("answers", payload),
            requireToken(),
        )
        Submission.parse(response.optJSONObject("submission") ?: JSONObject())
    }

    suspend fun ownSubmission(id: Int): SubmissionDetail = io {
        SubmissionDetail.parse(apiProvider().get("/api/student/submissions/$id", requireToken()))
    }

    suspend fun leave() = io {
        val token = requireToken()
        try {
            apiProvider().post("/api/student/leave", null, token)
        } finally {
            forget()
        }
    }

    fun forget() {
        token = null
        prefs.clearStudentSession()
    }

    private fun requireToken(): String =
        token ?: throw ApiException("UNAUTHORIZED", "Join a session first.", 401)
}

package com.sntch.app.data

import org.json.JSONArray
import org.json.JSONObject

/**
 * Wire models for the SNTCH local protocol. Field names mirror docs/PROTOCOL.md;
 * parsing is tolerant of absent optional fields but never invents data.
 */

internal fun JSONObject.str(key: String, fallback: String = ""): String =
    if (!has(key) || isNull(key)) fallback else optString(key, fallback)

internal fun JSONObject.int(key: String, fallback: Int = 0): Int =
    if (!has(key) || isNull(key)) fallback else optInt(key, fallback)

internal fun JSONObject.bool(key: String, fallback: Boolean = false): Boolean =
    if (!has(key) || isNull(key)) fallback else optBoolean(key, fallback)

internal fun JSONObject.long(key: String, fallback: Long = 0L): Long =
    if (!has(key) || isNull(key)) fallback else optLong(key, fallback)

internal fun JSONObject.array(key: String): JSONArray = optJSONArray(key) ?: JSONArray()

internal fun JSONArray.objects(): List<JSONObject> =
    (0 until length()).mapNotNull { optJSONObject(it) }

data class Student(
    val id: Int,
    val number: String,
    val name: String,
    val section: String,
) {
    companion object {
        fun parse(json: JSONObject) = Student(
            id = json.int("id"),
            number = json.str("number"),
            name = json.str("name"),
            section = json.str("section"),
        )
    }
}

data class Question(
    val id: Int,
    val type: String,
    val prompt: String,
    val options: List<String>,
    val correct: Int = -1,
    val keys: List<String> = emptyList(),
) {
    val isMultipleChoice: Boolean get() = type == "mc"

    companion object {
        fun parse(json: JSONObject) = Question(
            id = json.int("id"),
            type = json.str("type", "mc"),
            prompt = json.str("prompt"),
            options = json.array("options").let { array ->
                (0 until array.length()).map { array.optString(it) }
            },
            correct = json.int("correct", -1),
            keys = json.array("keys").let { array ->
                (0 until array.length()).map { array.optString(it) }
            },
        )
    }
}

data class Item(
    val id: Int,
    val kind: String,
    val title: String,
    val description: String,
    val instructions: String,
    val target: String,
    val status: String,
    val durationMin: Int,
    val passingPct: Int,
    val questionCount: Int,
    val questions: List<Question>,
) {
    val isExam: Boolean get() = kind == "exam"

    companion object {
        fun parse(json: JSONObject) = Item(
            id = json.int("id"),
            kind = json.str("kind", "activity"),
            title = json.str("title"),
            description = json.str("description"),
            instructions = json.str("instructions"),
            target = json.str("target"),
            status = json.str("status", "draft"),
            durationMin = json.int("durationMin"),
            passingPct = json.int("passingPct", 60),
            questionCount = json.int("questionCount"),
            questions = json.array("questions").objects().map { Question.parse(it) },
        )
    }
}

data class Session(
    val id: Int,
    val code: String,
    val refType: String,
    val refId: Int,
    val title: String,
    val target: String,
    val state: String,
    val expiresInSec: Int,
    val joined: List<Int>,
    val itemStatus: String,
) {
    val isExam: Boolean get() = refType == "exam"
    val acceptsWork: Boolean get() = state == "live"
    val open: Boolean get() = state == "ready" || state == "live" || state == "paused"

    companion object {
        fun parse(json: JSONObject) = Session(
            id = json.int("id"),
            code = json.str("code"),
            refType = json.str("refType", "activity"),
            refId = json.int("refId"),
            title = json.str("title"),
            target = json.str("target"),
            state = json.str("state", "ended"),
            expiresInSec = json.int("expiresInSec"),
            joined = json.array("joined").let { array ->
                (0 until array.length()).map { array.optInt(it) }
            },
            itemStatus = json.str("itemStatus"),
        )
    }
}

data class Submission(
    val id: Int,
    val refType: String,
    val refId: Int,
    val sessionId: Int,
    val studentId: Int,
    val studentName: String,
    val title: String,
    val score: Int,
    val total: Int,
    val percent: Int,
    val passed: Boolean,
    val timeout: Boolean,
    val status: String,
    val submittedMs: Long,
) {
    val isExam: Boolean get() = refType == "exam"

    companion object {
        fun parse(json: JSONObject) = Submission(
            id = json.int("id"),
            refType = json.str("refType", "activity"),
            refId = json.int("refId"),
            sessionId = json.int("sessionId"),
            studentId = json.int("studentId"),
            studentName = json.str("studentName"),
            title = json.str("title"),
            score = json.int("score"),
            total = json.int("total"),
            percent = json.int("percent"),
            passed = json.bool("passed"),
            timeout = json.bool("timeout"),
            status = json.str("status", "pending"),
            submittedMs = json.long("submittedMs"),
        )
    }
}

data class SubmissionDetail(
    val summary: Submission,
    val text: String,
    val answers: Map<String, String>,
    val answersVisible: Boolean,
    val questions: List<Question> = emptyList(),
) {
    fun questionFor(answerKey: String): Question? = questions.firstOrNull { it.id.toString() == answerKey }

    companion object {
        fun parse(json: JSONObject): SubmissionDetail {
            val answers = json.optJSONObject("answers")?.let { values ->
                values.keys().asSequence().associateWith { key ->
                    val value = values.opt(key)
                    if (value is Number) value.toString() else value?.toString() ?: ""
                }
            } ?: emptyMap()
            return SubmissionDetail(
                summary = Submission.parse(json),
                text = json.str("text"),
                answers = answers,
                answersVisible = json.bool("answersVisible", true),
                questions = json.array("questions").objects().map { Question.parse(it) },
            )
        }
    }
}

data class AttendanceDay(
    val date: String,
    val section: String,
    val marks: Map<Int, String>,
) {
    companion object {
        fun parse(json: JSONObject) = AttendanceDay(
            date = json.str("date"),
            section = json.str("section"),
            marks = json.array("marks").objects().associate {
                it.int("studentId") to it.str("status")
            },
        )
    }
}

data class DeviceInfo(
    val name: String,
    val ssid: String,
    val ip: String,
    val firmware: String,
    val uptimeMs: Long,
    val freeHeap: Int,
    val storageUsed: Int,
    val storageTotal: Int,
    val storageError: Boolean,
    val students: Int,
    val openSessions: Int,
) {
    companion object {
        val unknown = DeviceInfo("", "", "", "", 0, 0, 0, 0, false, 0, 0)

        fun parse(json: JSONObject) = DeviceInfo(
            name = json.str("name"),
            ssid = json.str("ssid"),
            ip = json.str("ip"),
            firmware = json.str("firmware"),
            uptimeMs = json.long("uptimeMs"),
            freeHeap = json.int("freeHeap"),
            storageUsed = json.int("storageUsed"),
            storageTotal = json.int("storageTotal"),
            storageError = json.bool("storageError"),
            students = json.int("students"),
            openSessions = json.int("openSessions"),
        )
    }
}

data class Event(val atMs: Long, val tone: String, val text: String) {
    companion object {
        fun parse(json: JSONObject) = Event(
            atMs = json.long("atMs"),
            tone = json.str("tone", "info"),
            text = json.str("text"),
        )
    }
}

data class TeacherState(
    val device: DeviceInfo,
    val students: List<Student>,
    val activities: List<Item>,
    val exams: List<Item>,
    val sessions: List<Session>,
    val submissions: List<Submission>,
    val attendance: List<AttendanceDay>,
    val events: List<Event>,
    val showCorrect: Boolean,
) {
    fun studentsIn(section: String) = students.filter { it.section == section }
    fun items() = exams + activities
    fun item(kind: String, id: Int): Item? =
        (if (kind == "exam") exams else activities).firstOrNull { it.id == id }
    fun session(id: Int): Session? = sessions.firstOrNull { it.id == id }
    fun openSessionFor(kind: String, id: Int): Session? =
        sessions.firstOrNull { it.refType == kind && it.refId == id && it.open }
    fun submissionsFor(kind: String, id: Int) =
        submissions.filter { it.refType == kind && it.refId == id }

    fun attendanceFor(date: String, section: String): Map<Int, String> =
        attendance.firstOrNull { it.date == date && it.section == section }?.marks ?: emptyMap()

    companion object {
        fun parse(root: JSONObject): TeacherState {
            val settings = root.optJSONObject("settings") ?: JSONObject()
            return TeacherState(
                device = (root.optJSONObject("device") ?: JSONObject()).let { DeviceInfo.parse(it) },
                students = root.array("students").objects().map { Student.parse(it) },
                activities = root.array("activities").objects().map { Item.parse(it) },
                exams = root.array("exams").objects().map { Item.parse(it) },
                sessions = root.array("sessions").objects().map { Session.parse(it) },
                submissions = root.array("submissions").objects().map { Submission.parse(it) },
                attendance = root.array("attendance").objects().map { AttendanceDay.parse(it) },
                events = root.array("events").objects().map { Event.parse(it) },
                showCorrect = settings.bool("showCorrect", true),
            )
        }
    }
}

data class StudentWork(
    val kind: String,
    val id: Int,
    val title: String,
    val instructions: String,
    val durationMin: Int,
    val passingPct: Int,
    val questionCount: Int,
    val status: String,
    val submitted: Boolean,
) {
    val isExam: Boolean get() = kind == "exam"

    companion object {
        fun parse(json: JSONObject) = StudentWork(
            kind = json.str("kind", "activity"),
            id = json.int("id"),
            title = json.str("title"),
            instructions = json.str("instructions"),
            durationMin = json.int("durationMin"),
            passingPct = json.int("passingPct"),
            questionCount = json.int("questionCount"),
            status = json.str("status"),
            submitted = json.bool("submitted"),
        )
    }
}

data class AttemptRef(val examId: Int, val sessionId: Int, val remainingSec: Int)

data class StudentSessionInfo(
    val code: String,
    val title: String,
    val refType: String,
    val refId: Int,
    val target: String,
    val state: String,
    val expiresInSec: Int,
) {
    companion object {
        fun parse(json: JSONObject) = StudentSessionInfo(
            code = json.str("code"),
            title = json.str("title"),
            refType = json.str("refType", "activity"),
            refId = json.int("refId"),
            target = json.str("target"),
            state = json.str("state", "ended"),
            expiresInSec = json.int("expiresInSec"),
        )
    }
}

data class StudentState(
    val me: Student,
    val session: StudentSessionInfo?,
    val availableWork: List<StudentWork>,
    val mySubmissions: List<Submission>,
    val activeAttempt: AttemptRef?,
    val showCorrect: Boolean,
) {
    companion object {
        fun parse(root: JSONObject): StudentState {
            val sessionJson = root.optJSONObject("session")
            val attemptJson = root.optJSONObject("activeAttempt")
            val policy = root.optJSONObject("resultsPolicy") ?: JSONObject()
            return StudentState(
                me = Student.parse(root.optJSONObject("me") ?: JSONObject()),
                session = sessionJson?.let { StudentSessionInfo.parse(it) },
                availableWork = root.array("availableWork").objects().map { StudentWork.parse(it) },
                mySubmissions = root.array("mySubmissions").objects().map { Submission.parse(it) },
                activeAttempt = attemptJson?.let {
                    AttemptRef(it.int("examId"), it.int("sessionId"), it.int("remainingSec"))
                },
                showCorrect = policy.bool("showCorrect", true),
            )
        }
    }
}

data class ExamAttempt(
    val examId: Int,
    val title: String,
    val durationMin: Int,
    val passingPct: Int,
    val remainingSec: Int,
    val questions: List<Question>,
    val answers: MutableMap<String, Any>,
)

data class SessionPreview(
    val code: String,
    val title: String,
    val refType: String,
    val target: String,
    val state: String,
    val expiresInSec: Int,
) {
    companion object {
        fun parse(json: JSONObject) = SessionPreview(
            code = json.str("code"),
            title = json.str("title"),
            refType = json.str("refType", "activity"),
            target = json.str("target"),
            state = json.str("state"),
            expiresInSec = json.int("expiresInSec"),
        )
    }
}

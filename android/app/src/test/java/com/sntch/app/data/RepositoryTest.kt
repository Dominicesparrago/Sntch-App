package com.sntch.app.data

import kotlinx.coroutines.runBlocking
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test

class RepositoryTest {

    private lateinit var controller: FakeController
    private lateinit var store: FakeStore

    @Before
    fun setUp() {
        controller = FakeController()
        controller.start()
        store = FakeStore()
    }

    @After
    fun tearDown() {
        controller.stop()
    }

    private fun teacher() = TeacherRepository({ SntchApi(controller.baseUrl) }, store)
    private fun student() = StudentRepository({ SntchApi(controller.baseUrl) }, store)

    @Test
    fun `teacher login stores the token and reads the snapshot`() = runBlocking {
        controller.route(
            "/api/teacher/login",
            body = """{"ok":true,"token":"teacher-token","expiresInSec":28800,
                       "device":{"name":"SNTCH-AB","ssid":"SNTCH-EXAM","ip":"192.168.4.1","firmware":"0.1.0"}}""",
        )
        controller.route("/api/teacher/state", body = emptyTeacherStateJson())

        val repo = teacher()
        val device = repo.login("246810")
        assertEquals("SNTCH-AB", device.name)
        assertEquals("teacher-token", store.teacherToken)
        assertTrue(repo.signedIn)

        val state = repo.state()
        assertEquals(2, state.students.size)
        assertEquals("Bearer teacher-token", controller.lastRequest().authorization)
    }

    @Test
    fun `teacher 401 clears the stored token`() = runBlocking {
        store.teacherToken = "expired"
        controller.route(
            "/api/teacher/state",
            status = 401,
            body = """{"ok":false,"error":{"code":"TOKEN_EXPIRED","message":"Sign in again."}}""",
        )
        val repo = teacher()
        try {
            repo.state()
            fail("expected ApiException")
        } catch (error: ApiException) {
            assertEquals(401, error.status)
        }
        assertFalse(repo.signedIn)
        assertNull(store.teacherToken)
    }

    @Test
    fun `roster import upserts a typed student list`() = runBlocking {
        store.teacherToken = "t"
        controller.route("/api/teacher/students", body = """{"ok":true,"added":1,"updated":1}""")
        val (added, updated) = teacher().importStudents(
            listOf(Triple("001", "Ana Cruz", "9-A"), Triple("002", "Ben Lim", "9-A")),
        )
        assertEquals(1, added)
        assertEquals(1, updated)
        val body = JSONObject(controller.lastRequest().body)
        val students = body.getJSONArray("students")
        assertEquals(2, students.length())
        assertEquals("Ana Cruz", students.getJSONObject(0).getString("name"))
        assertEquals("9-A", students.getJSONObject(1).getString("section"))
    }

    @Test
    fun `exam creation sends question shapes the controller validates`() = runBlocking {
        store.teacherToken = "t"
        controller.route("/api/teacher/exams", body = """{"ok":true,"id":11}""")
        val id = teacher().createExam(
            title = "Quiz",
            instructions = "Answer all",
            target = "9-A",
            durationMin = 15,
            passingPct = 60,
            questions = listOf(
                NewQuestion("mc", "2+2?", listOf("3", "4"), correct = 1),
                NewQuestion("text", "Gas?", keys = listOf("Oxygen", "O2")),
            ),
        )
        assertEquals(11, id)
        val body = JSONObject(controller.lastRequest().body)
        val questions = body.getJSONArray("questions")
        assertEquals("mc", questions.getJSONObject(0).getString("type"))
        assertEquals(1, questions.getJSONObject(0).getInt("correct"))
        assertEquals(2, questions.getJSONObject(0).getJSONArray("options").length())
        assertEquals("O2", questions.getJSONObject(1).getJSONArray("keys").getString(1))
        assertEquals(15, body.getInt("durationMin"))
    }

    @Test
    fun `session actions hit the documented paths`() = runBlocking {
        store.teacherToken = "t"
        controller.route("/api/teacher/sessions", body = """{"ok":true,"id":7}""")
        controller.route("/api/teacher/sessions/7/start", body = """{"ok":true}""")
        val repo = teacher()
        assertEquals(7, repo.createSession("exam", 11, 30))
        repo.sessionAction(7, "start")
        assertEquals("/api/teacher/sessions/7/start", controller.lastRequest().path)
        assertEquals("POST", controller.lastRequest().method)
    }

    @Test
    fun `attendance mark-all includes the section and status`() = runBlocking {
        store.teacherToken = "t"
        controller.route("/api/teacher/attendance", body = """{"ok":true}""")
        teacher().markAttendance("2026-10-25", "9-A", markAll = "present")
        val body = JSONObject(controller.lastRequest().body)
        assertEquals("2026-10-25", body.getString("date"))
        assertEquals("9-A", body.getString("section"))
        assertEquals("present", body.getString("markAll"))
        assertFalse(body.has("studentId"))
    }

    @Test
    fun `student join stores identity then loads state`() = runBlocking {
        controller.route(
            "/api/student/join",
            body = """{"ok":true,"studentToken":"student-token",
                       "student":{"id":1,"number":"001","name":"Ana Cruz","section":"9-A"}}""",
        )
        controller.route("/api/student/state", body = studentStateJson())

        val repo = student()
        val state = repo.join("snt-abcd", "001")
        assertEquals("Ana Cruz", state.me.name)
        assertEquals("live", state.session?.state)
        assertEquals("student-token", store.studentToken)
        assertEquals("Ana Cruz", store.studentName)
        assertEquals("9-A", store.studentSection)

        val joinBody = controller.requests.first { it.path.startsWith("/api/student/join") }.body
        assertEquals("SNT-ABCD", JSONObject(joinBody).getString("code"))
    }

    @Test
    fun `student 401 forgets the joined session`() = runBlocking {
        store.studentToken = "old"
        controller.route(
            "/api/student/state",
            status = 401,
            body = """{"ok":false,"error":{"code":"SESSION_CLOSED","message":"This session has ended."}}""",
        )
        val repo = student()
        try {
            repo.state()
            fail("expected ApiException")
        } catch (error: ApiException) {
            assertEquals(401, error.status)
        }
        assertFalse(repo.joined)
        assertNull(store.studentToken)
    }

    @Test
    fun `student exam submission sends answers and parses the grade`() = runBlocking {
        store.studentToken = "s"
        controller.route(
            "/api/student/submit",
            body = """{"ok":true,"duplicate":false,
                       "submission":{"id":9,"refType":"exam","refId":11,"sessionId":3,"studentId":1,
                                     "studentName":"Ana Cruz","score":2,"total":2,"percent":100,
                                     "passed":true,"timeout":false,"status":"graded","submittedMs":0,
                                     "title":"Quiz"}}""",
        )
        val submission = student().submitExam(11, mapOf("100" to 1, "101" to "Oxygen"))
        assertEquals(100, submission.percent)
        assertTrue(submission.passed)
        val body = JSONObject(controller.lastRequest().body)
        assertEquals(11, body.getInt("refId"))
        assertEquals(1, body.getJSONObject("answers").getInt("100"))
    }

    @Test
    fun `autosave posts only the answer map`() = runBlocking {
        store.studentToken = "s"
        controller.route("/api/student/attempts/11/answers", body = """{"ok":true}""")
        student().saveAnswers(11, mapOf("100" to 1))
        assertEquals("/api/student/attempts/11/answers", controller.lastRequest().path)
        val body = JSONObject(controller.lastRequest().body)
        assertEquals(1, body.getJSONObject("answers").getInt("100"))
    }

    @Test
    fun `submission detail keeps question prompts but not answer keys`() = runBlocking {
        store.studentToken = "s"
        controller.route(
            "/api/student/submissions/9",
            body = """{"ok":true,"id":9,"refType":"exam","refId":11,"studentId":1,"score":1,"total":1,
                       "percent":100,"passed":true,"status":"graded","answersVisible":true,
                       "answers":{"100":"1"},
                       "questions":[{"id":100,"type":"mc","prompt":"2+2?","options":["3","4"]}]}""",
        )
        val detail = student().ownSubmission(9)
        assertEquals("2+2?", detail.questionFor("100")?.prompt)
        assertEquals("1", detail.answers["100"])
        assertEquals(-1, detail.questionFor("100")?.correct)
    }

    @Test
    fun `unreachable controller surfaces a helpful message`() {
        val repo = TeacherRepository({ SntchApi("http://127.0.0.1:1") }, store)
        assertEquals(
            "Cannot reach the SNTCH controller. Check that this device is connected to the SNTCH Wi-Fi network.",
            describeFailure(NetworkException(
                "Cannot reach the SNTCH controller. Check that this device is connected to the SNTCH Wi-Fi network.",
            )),
        )
        assertFalse(repo.signedIn)
    }
}

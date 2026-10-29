package com.sntch.app.data

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ModelsTest {

    @Test
    fun `teacher state parses the controller snapshot`() {
        val state = TeacherState.parse(JSONObject(emptyTeacherStateJson()))

        assertEquals("SNTCH-AB", state.device.name)
        assertEquals(2, state.students.size)
        assertEquals("9-B", state.students[1].section)
        assertEquals(1, state.exams.size)
        assertEquals(1, state.activities.size)
        assertEquals(2, state.items().size)
        assertEquals(1, state.attendance.size)
        assertEquals("present", state.attendanceFor("2026-10-25", "9-A")[1])
        assertTrue(state.showCorrect)
        assertEquals(1, state.events.size)
        assertEquals("Warm-up", state.item("activity", 10)?.title)
        assertNull(state.item("exam", 10))
    }

    @Test
    fun `exam questions keep answer keys only for teacher snapshots`() {
        val exam = TeacherState.parse(JSONObject(emptyTeacherStateJson())).exams.first()
        val question = exam.questions.first()
        assertEquals(100, question.id)
        assertTrue(question.isMultipleChoice)
        assertEquals(listOf("3", "4"), question.options)
        assertEquals(1, question.correct)
    }

    @Test
    fun `sessions parse membership and state`() {
        val sessions = """
            [{"id":3,"code":"SNT-ABCD","refType":"exam","refId":11,"title":"Quiz","target":"9-A",
              "state":"paused","expiresInSec":1200,"joined":[1,2],"itemStatus":"active"}]
        """.trimIndent()
        val state = TeacherState.parse(JSONObject(emptyTeacherStateJson(sessions = sessions)))
        val session = state.sessions.single()
        assertEquals(listOf(1, 2), session.joined)
        assertTrue(session.open)
        assertFalse(session.acceptsWork)
        assertEquals(session, state.openSessionFor("exam", 11))
    }

    @Test
    fun `pending deletions parse backup metadata`() {
        val sessions = """
            [{"id":3,"code":"SNT-ABCD","refType":"exam","refId":11,"title":"Quiz","target":"9-A",
              "state":"pending_delete","expiresInSec":0,"joined":[],"itemStatus":"active"}]
        """.trimIndent()
        val pending = """
            [{"sessionId":3,"previousState":"ended","requestedMs":50000,"code":"SNT-ABCD",
              "title":"Quiz","refType":"exam","refId":11,"backupPath":"/del/3.json",
              "backupPresent":true,"backupBytes":2048}]
        """.trimIndent()
        val state = TeacherState.parse(
            JSONObject(emptyTeacherStateJson(sessions = sessions, pendingDeletions = pending)),
        )
        val deletion = state.pendingDeletions.single()
        assertEquals(3, deletion.sessionId)
        assertEquals("ended", deletion.previousState)
        assertEquals("/del/3.json", deletion.backupPath)
        assertTrue(deletion.backupPresent)
        assertEquals(2048, deletion.backupBytes)
        assertTrue(state.sessions.single().pendingDelete)
        assertFalse(state.sessions.single().open)
        assertFalse(state.sessions.single().deleteEligible)
    }

    @Test
    fun `submission summaries parse grading fields`() {
        val submissions = """
            [{"id":9,"refType":"exam","refId":11,"sessionId":3,"studentId":1,"studentName":"Ana Cruz",
              "score":1,"total":1,"percent":100,"passed":true,"timeout":false,"status":"graded",
              "submittedMs":50000,"title":"Quiz"}]
        """.trimIndent()
        val state = TeacherState.parse(JSONObject(emptyTeacherStateJson(submissions = submissions)))
        val submission = state.submissions.single()
        assertEquals(100, submission.percent)
        assertTrue(submission.passed)
        assertTrue(submission.isExam)
        assertEquals("Ana Cruz", submission.studentName)
    }

    @Test
    fun `student state handles a null session`() {
        val json = JSONObject(studentStateJson())
        json.put("session", JSONObject.NULL)
        json.put("activeAttempt", JSONObject.NULL)
        val state = StudentState.parse(json)
        assertNull(state.session)
        assertNull(state.activeAttempt)
        assertEquals("9-A", state.me.section)
        assertEquals(1, state.availableWork.size)
    }

    @Test
    fun `student state parses session and attempt`() {
        val state = StudentState.parse(JSONObject(studentStateJson()))
        assertEquals("SNT-ABCD", state.session?.code)
        assertEquals("live", state.session?.state)
        assertEquals(900, state.activeAttempt?.remainingSec)
        assertFalse(state.availableWork.first().submitted)
    }

    @Test
    fun `submission detail links answers to prompts`() {
        val questions = listOf(
            Question(id = 100, type = "mc", prompt = "2+2?", options = listOf("3", "4"), correct = 1),
        )
        val detail = SubmissionDetail(
            summary = Submission(
                id = 1, refType = "exam", refId = 11, sessionId = 3, studentId = 1,
                studentName = "Ana", title = "Quiz", score = 1, total = 1, percent = 100,
                passed = true, timeout = false, status = "graded", submittedMs = 0,
            ),
            text = "",
            answers = mapOf("100" to "1"),
            answersVisible = true,
            questions = questions,
        )
        assertEquals("2+2?", detail.questionFor("100")?.prompt)
        assertNull(detail.questionFor("999"))
    }

    @Test
    fun `missing optional fields fall back instead of throwing`() {
        val state = TeacherState.parse(JSONObject("""{"ok":true}"""))
        assertEquals(0, state.students.size)
        assertEquals(0, state.sessions.size)
        assertTrue(state.showCorrect)
        assertEquals("", state.device.name)
    }
}

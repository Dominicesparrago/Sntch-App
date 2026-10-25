package com.sntch.app.data

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Parses responses captured from the actual firmware core (via tools/host_sim)
 * with the Android models. This is the C++ <-> Kotlin protocol compatibility
 * check: if either side renames a field, these tests fail.
 *
 * Regenerate with: python tools/capture_fixtures.py
 */
class WireFixtureTest {

    private fun fixture(name: String): String =
        javaClass.classLoader!!.getResourceAsStream("controller/$name")!!
            .bufferedReader()
            .use { it.readText() }

    private fun json(name: String) = JSONObject(fixture(name))

    @Test
    fun `health response from the real controller parses`() {
        val health = json("health.json")
        assertTrue(health.getBoolean("ok"))
        assertEquals(2, health.getInt("students"))
        assertFalse(health.getBoolean("storageError"))
        assertEquals("0.1.0-host-sim", health.str("firmware"))
        assertTrue(health.str("deviceName").startsWith("SNTCH-"))
    }

    @Test
    fun `teacher snapshot from the real controller parses`() {
        val state = TeacherState.parse(json("teacher_state.json"))
        assertEquals(2, state.students.size)
        assertEquals("Ana Cruz", state.students.first().name)
        assertEquals(1, state.exams.size)
        val exam = state.exams.single()
        assertEquals(2, exam.questions.size)
        assertEquals(listOf("Light", "Sound"), exam.questions.first().options)
        assertEquals(0, exam.questions.first().correct)
        assertEquals("Photosynthesis check", exam.title)

        assertEquals(1, state.sessions.size)
        val session = state.sessions.single()
        assertTrue(session.code.startsWith("SNT-"))
        assertEquals("exam", session.refType)
        assertTrue(session.joined.isNotEmpty())

        assertEquals(1, state.submissions.size)
        assertEquals(100, state.submissions.single().percent)
        assertEquals("SNTCH-SIM", state.device.name)
    }

    @Test
    fun `teacher submission detail from the real controller has prompts but no keys`() {
        val raw = json("teacher_submission.json")
        val detail = SubmissionDetail.parse(raw)
        assertEquals(2, detail.questions.size)
        assertEquals("What do chloroplasts capture?", detail.questions[0].prompt)
        assertEquals(-1, detail.questions[0].correct)
        assertTrue(detail.questions[0].keys.isEmpty())
        assertEquals("0", detail.answers["1"])
        assertEquals(100, detail.summary.percent)
    }

    @Test
    fun `student submission detail exposes keys when the policy allows`() {
        val detail = SubmissionDetail.parse(json("student_submission.json"))
        assertTrue(detail.answersVisible)
        assertEquals("What do chloroplasts capture?", detail.questionFor("1")?.prompt)
        assertEquals(0, detail.questionFor("1")?.correct)
        assertEquals("oxygen", detail.answers["2"])
        assertTrue(detail.summary.passed)
    }

    @Test
    fun `student state from the real controller parses`() {
        val state = StudentState.parse(json("student_state.json"))
        assertEquals("Ana Cruz", state.me.name)
        assertEquals("9-A", state.me.section)
        assertEquals("live", state.session?.state)
        assertTrue(state.session?.code?.startsWith("SNT-") == true)
        assertEquals(1, state.mySubmissions.size)
        assertEquals(100, state.mySubmissions.single().percent)
        assertNull(state.activeAttempt)
        assertTrue(state.availableWork.all { it.submitted })
    }

    @Test
    fun `exam start response from the real controller never carries answer keys`() {
        val attempt = json("exam_start.json").getJSONObject("attempt")
        val questions = attempt.getJSONArray("questions")
        assertEquals(2, questions.length())
        for (index in 0 until questions.length()) {
            val question = questions.getJSONObject(index)
            assertFalse(question.has("correct"))
            assertFalse(question.has("keys"))
            assertTrue(question.getString("prompt").isNotEmpty())
        }
        assertTrue(attempt.getInt("remainingSec") > 0)
    }
}

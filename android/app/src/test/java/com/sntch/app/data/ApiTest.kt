package com.sntch.app.data

import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test

class ApiTest {

    private lateinit var controller: FakeController

    @Before
    fun setUp() {
        controller = FakeController()
        controller.start()
    }

    @After
    fun tearDown() {
        controller.stop()
    }

    @Test
    fun `health reads the controller status`() {
        controller.route(
            "/api/health",
            body = """{"ok":true,"firmware":"0.1.0","deviceName":"SNTCH-AB","students":3,
                       "activeSessions":1,"storageError":false}""",
        )
        val health = SntchApi(controller.baseUrl).health()
        assertEquals("0.1.0", health.str("firmware"))
        assertEquals(3, health.int("students"))
        assertEquals("GET", controller.lastRequest().method)
    }

    @Test
    fun `error envelope becomes ApiException with code and status`() {
        controller.route(
            "/api/student/submit",
            status = 409,
            body = """{"ok":false,"error":{"code":"SESSION_NOT_LIVE","message":"The session is not live right now."}}""",
        )
        try {
            SntchApi(controller.baseUrl).post("/api/student/submit")
            fail("expected ApiException")
        } catch (error: ApiException) {
            assertEquals(409, error.status)
            assertEquals("SESSION_NOT_LIVE", error.code)
            assertEquals("The session is not live right now.", error.message)
        }
    }

    @Test
    fun `bearer token is sent on authenticated requests`() {
        controller.route("/api/teacher/state", body = emptyTeacherStateJson())
        SntchApi(controller.baseUrl).get("/api/teacher/state", "token-123")
        assertEquals("Bearer token-123", controller.lastRequest().authorization)
    }

    @Test
    fun `post sends a JSON body`() {
        controller.route("/api/teacher/login", body = """{"ok":true,"token":"abc"}""")
        SntchApi(controller.baseUrl).post("/api/teacher/login", org.json.JSONObject().put("pin", "246810"))
        assertEquals("POST", controller.lastRequest().method)
        assertTrue(controller.lastRequest().body.contains("\"pin\":\"246810\""))
    }

    @Test
    fun `unreadable success response is a network error`() {
        controller.route("/api/health", body = "<html>not json</html>")
        try {
            SntchApi(controller.baseUrl).health()
            fail("expected NetworkException")
        } catch (error: NetworkException) {
            assertTrue(error.message!!.contains("unreadable"))
        }
    }

    @Test
    fun `unreachable controller is a network error`() {
        try {
            SntchApi("http://127.0.0.1:1").health()
            fail("expected NetworkException")
        } catch (error: NetworkException) {
            assertTrue(error.message!!.contains("Cannot reach"))
        }
    }

    @Test
    fun `ok false is rejected even with HTTP 200`() {
        controller.route("/api/health", body = """{"ok":false}""")
        try {
            SntchApi(controller.baseUrl).health()
            fail("expected ApiException")
        } catch (error: ApiException) {
            assertEquals("UNEXPECTED", error.code)
        }
    }
}

package com.sntch.app.data

import com.sun.net.httpserver.HttpExchange
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress

/** In-memory LocalStore so repositories can be tested without Android. */
class FakeStore : LocalStore {
    override var baseUrl: String = "http://127.0.0.1:1"
    override var role: String? = null
    override var teacherToken: String? = null
    override var studentToken: String? = null
    override var studentName: String = ""
    override var studentNumber: String = ""
    override var studentSection: String = ""
    override var showCorrect: Boolean = true
    private val activityDrafts = mutableMapOf<Int, String>()
    private val examDrafts = mutableMapOf<Int, Map<String, Any>>()

    override fun activityDraft(id: Int): String = activityDrafts[id] ?: ""
    override fun saveActivityDraft(id: Int, text: String) {
        activityDrafts[id] = text
    }

    override fun examDraft(examId: Int): Map<String, Any> = examDrafts[examId] ?: emptyMap()
    override fun saveExamDraft(examId: Int, answers: Map<String, Any>) {
        examDrafts[examId] = answers
    }

    override fun clearExamDraft(examId: Int) {
        examDrafts.remove(examId)
    }

    override fun clearStudentSession() {
        studentToken = null
        studentSection = ""
    }

    override fun forgetTeacher() {
        teacherToken = null
    }
}

data class RecordedRequest(
    val method: String,
    val path: String,
    val authorization: String?,
    val body: String,
)

/**
 * A real loopback HTTP server. The app's SntchApi talks to it over sockets, so
 * tests cover transport, JSON envelopes and repository behaviour together.
 */
class FakeController {
    private val server: HttpServer = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
    val requests = mutableListOf<RecordedRequest>()

    val baseUrl: String get() = "http://127.0.0.1:${server.address.port}"

    fun route(path: String, status: Int = 200, body: String) {
        server.createContext(path) { exchange: HttpExchange ->
            val requestBody = exchange.requestBody.bufferedReader().use { it.readText() }
            requests += RecordedRequest(
                method = exchange.requestMethod,
                path = exchange.requestURI.path,
                authorization = exchange.requestHeaders.getFirst("Authorization"),
                body = requestBody,
            )
            val bytes = body.toByteArray(Charsets.UTF_8)
            exchange.responseHeaders.add("Content-Type", "application/json")
            exchange.sendResponseHeaders(status, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
    }

    fun routeSequence(path: String, responses: List<Pair<Int, String>>) {
        var index = 0
        server.createContext(path) { exchange: HttpExchange ->
            val requestBody = exchange.requestBody.bufferedReader().use { it.readText() }
            requests += RecordedRequest(
                method = exchange.requestMethod,
                path = exchange.requestURI.path,
                authorization = exchange.requestHeaders.getFirst("Authorization"),
                body = requestBody,
            )
            val response = responses[index.coerceAtMost(responses.size - 1)]
            index += 1
            val bytes = response.second.toByteArray(Charsets.UTF_8)
            exchange.responseHeaders.add("Content-Type", "application/json")
            exchange.sendResponseHeaders(response.first, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
    }

    fun start() {
        server.start()
    }

    fun stop() {
        server.stop(0)
    }

    fun lastRequest(): RecordedRequest = requests.last()
}

fun emptyTeacherStateJson(
    sessions: String = "[]",
    submissions: String = "[]",
): String = """
    {
      "ok": true,
      "device": {"name":"SNTCH-AB","ssid":"SNTCH-EXAM","ip":"192.168.4.1","firmware":"0.1.0",
                 "uptimeMs":120000,"freeHeap":180000,"storageUsed":4096,"storageTotal":1048576,
                 "storageError":false,"students":2,"openSessions":1},
      "students": [
        {"id":1,"number":"001","name":"Ana Cruz","section":"9-A"},
        {"id":2,"number":"002","name":"Ben Lim","section":"9-B"}
      ],
      "activities": [
        {"id":10,"kind":"activity","title":"Warm-up","description":"Short task","instructions":"Two sentences.",
         "target":"9-A","status":"active","durationMin":0,"passingPct":60,"questionCount":0,"questions":[]}
      ],
      "exams": [
        {"id":11,"kind":"exam","title":"Quiz","instructions":"Answer all","target":"9-A","status":"active",
         "durationMin":15,"passingPct":60,"questionCount":1,
         "questions":[{"id":100,"type":"mc","prompt":"2+2?","options":["3","4"],"correct":1,"keys":[]}]}
      ],
      "sessions": $sessions,
      "submissions": $submissions,
      "attendance": [
        {"date":"2026-10-25","section":"9-A","marks":[{"studentId":1,"status":"present"}]}
      ],
      "events": [{"atMs":1000,"tone":"ok","text":"Ana Cruz joined SNT-ABCD"}],
      "settings": {"showCorrect": true}
    }
""".trimIndent()

fun studentStateJson(): String = """
    {
      "ok": true,
      "me": {"id":1,"number":"001","name":"Ana Cruz","section":"9-A"},
      "session": {"code":"SNT-ABCD","title":"Quiz","refType":"exam","refId":11,"target":"9-A",
                  "state":"live","expiresInSec":1500},
      "availableWork": [
        {"kind":"exam","id":11,"title":"Quiz","instructions":"Answer all","durationMin":15,
         "passingPct":60,"questionCount":1,"status":"active","submitted":false}
      ],
      "mySubmissions": [],
      "activeAttempt": {"examId":11,"sessionId":3,"remainingSec":900},
      "resultsPolicy": {"showCorrect": true}
    }
""".trimIndent()

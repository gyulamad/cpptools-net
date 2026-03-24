#pragma once

#include "TcpServer.hpp"
#include <string>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cctype>

using namespace std;

// =============================================================================
// HttpRequest
// =============================================================================
struct HttpRequest {
    string method;                         // GET, POST, PUT, DELETE, …
    string path;                           // /foo/bar
    string query;                          // everything after '?' (raw)
    string version;                        // HTTP/1.0 or HTTP/1.1
    unordered_map<string, string> headers; // lower-cased names
    string body;

    string header(const string& name, const string& fallback = "") const {
        string lower = name;
        transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        auto it = headers.find(lower);
        return it != headers.end() ? it->second : fallback;
    }

    bool keepAlive() const {
        string conn = header("connection");
        transform(conn.begin(), conn.end(), conn.begin(), ::tolower);
        if (conn == "close")      return false;
        if (conn == "keep-alive") return true;
        return version == "HTTP/1.1";
    }
};

// =============================================================================
// HttpResponse
// =============================================================================
struct HttpResponse {
    int    status = 200;
    string reason = "OK";
    unordered_map<string, string> headers;
    string body;

    HttpResponse& setStatus(int code, const string& text) {
        status = code; reason = text; return *this;
    }
    HttpResponse& setHeader(const string& name, const string& value) {
        headers[name] = value; return *this;
    }
    HttpResponse& setBody(const string& content,
                          const string& contentType = "text/plain") {
        body = content;
        headers["Content-Type"]   = contentType;
        headers["Content-Length"] = to_string(content.size());
        return *this;
    }

    string serialize() const {
        ostringstream oss;
        oss << "HTTP/1.1 " << status << " " << reason << "\r\n";
        for (auto& [k, v] : headers) oss << k << ": " << v << "\r\n";
        oss << "\r\n" << body;
        return oss.str();
    }

    // == Static factories ======================================================
    static HttpResponse ok(const string& body, const string& ct = "text/plain") {
        return HttpResponse{}.setStatus(200, "OK").setBody(body, ct);
    }
    static HttpResponse html(const string& body) {
        return ok(body, "text/html");
    }
    static HttpResponse json(const string& body) {
        return ok(body, "application/json");
    }
    static HttpResponse notFound(const string& msg = "Not Found") {
        return HttpResponse{}.setStatus(404, "Not Found").setBody(msg);
    }
    static HttpResponse badRequest(const string& msg = "Bad Request") {
        return HttpResponse{}.setStatus(400, "Bad Request").setBody(msg);
    }
    static HttpResponse internalError(const string& msg = "Internal Server Error") {
        return HttpResponse{}.setStatus(500, "Internal Server Error").setBody(msg);
    }
    static HttpResponse methodNotAllowed() {
        return HttpResponse{}.setStatus(405, "Method Not Allowed").setBody("Method Not Allowed");
    }
};

// =============================================================================
// HttpServer
//
//   Implements HTTP/1.1 framing over TcpServer via onRawData().
//   Accumulates bytes into a per-connection parse state machine:
//     RequestLine → Headers → Body → dispatch → (reset for keep-alive)
//
//   Subclass and implement onHttpRequest().
// =============================================================================
class HttpServer : public TcpServer {
public:
    HttpServer() = default;
    virtual ~HttpServer() = default;

protected:
    // == Implement this ========================================================

    virtual void onHttpRequest(int fd, const HttpRequest& req) = 0;

    // Default: send 400 + close.  Override to customise error handling.
    virtual void onHttpError(int fd, int /*statusCode*/, const string& msg) {
        HttpResponse res = HttpResponse::badRequest(msg);
        res.setHeader("Connection", "close");
        sendHttpResponse(fd, res);
        closeAfterFlush(fd);
    }

    // == Optional connection lifecycle hooks ===================================
    virtual void onHttpClientConnect(int /*fd*/, const string& /*remoteAddr*/) {}
    virtual void onHttpClientDisconnect(int /*fd*/) {}
    virtual void onHttpClientError(int /*fd*/, const string& /*error*/) {}

    // == Utilities =============================================================

    void sendHttpResponse(int fd, const HttpResponse& res) {
        sendToClient(fd, res.serialize());
    }

    void sendHttpResponse(int fd, int status, const string& body,
                          const string& contentType = "text/plain") {
        HttpResponse res;
        res.status = status;
        res.reason = defaultReason(status);
        res.setBody(body, contentType);
        sendHttpResponse(fd, res);
    }

    // == TcpServer hooks =======================================================

    void onClientConnect(int fd, const string& remoteAddr) override {
        parseStates[fd] = ParseState{};
        onHttpClientConnect(fd, remoteAddr);
    }

    void onClientDisconnect(int fd) override {
        parseStates.erase(fd);
        onHttpClientDisconnect(fd);
    }

    void onClientError(int fd, const string& error) override {
        parseStates.erase(fd);
        onHttpClientError(fd, error);
    }

    // == HTTP framing — owns everything from raw bytes to dispatched request ===
    void onRawData(int fd, string& buf) override {
        // A single TCP chunk may contain multiple pipelined requests,
        // so we loop until we can't make progress.
        while (!buf.empty()) {
            auto it = parseStates.find(fd);
            if (it == parseStates.end()) return;
            ParseState& ps = it->second;

            if (ps.stage == Stage::Body) {
                if (!consumeBody(fd, ps, buf)) return; // need more bytes
                // consumeBody dispatched the request and reset ps to RequestLine
            } else {
                // Header stages: consume one line at a time
                auto pos = buf.find('\n');
                if (pos == string::npos) return; // incomplete line — wait

                string line = buf.substr(0, pos);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                buf.erase(0, pos + 1);

                if (ps.stage == Stage::RequestLine)
                    parseRequestLine(fd, ps, line);
                else
                    parseHeaderLine(fd, ps, line);
            }

            // parseStates[fd] may have been erased by an error/disconnect
            if (!parseStates.count(fd)) return;
        }
    }

private:
    enum class Stage { RequestLine, Headers, Body };

    struct ParseState {
        Stage       stage = Stage::RequestLine;
        HttpRequest request;
        size_t      bodyRemaining = 0;
    };

    unordered_map<int, ParseState> parseStates;

    // == Request-line: "GET /path?query HTTP/1.1" =============================
    void parseRequestLine(int fd, ParseState& ps, const string& line) {
        if (line.empty()) return; // tolerate leading blank lines

        istringstream iss(line);
        string target;
        if (!(iss >> ps.request.method >> target >> ps.request.version)) {
            onHttpError(fd, 400, "Malformed request line: " + line);
            return;
        }

        auto qpos = target.find('?');
        if (qpos != string::npos) {
            ps.request.path  = target.substr(0, qpos);
            ps.request.query = target.substr(qpos + 1);
        } else {
            ps.request.path = target;
        }

        ps.stage = Stage::Headers;
    }

    // == Header line or blank line (end of headers) ============================
    void parseHeaderLine(int fd, ParseState& ps, const string& line) {
        if (!line.empty()) {
            auto colon = line.find(':');
            if (colon == string::npos) {
                onHttpError(fd, 400, "Malformed header: " + line);
                return;
            }
            string name  = line.substr(0, colon);
            string value = line.substr(colon + 1);

            auto start = value.find_first_not_of(" \t");
            auto end   = value.find_last_not_of(" \t");
            value = (start == string::npos) ? "" : value.substr(start, end - start + 1);

            transform(name.begin(), name.end(), name.begin(), ::tolower);
            ps.request.headers[name] = value;
            return;
        }

        // Blank line — end of headers
        string lenStr = ps.request.header("content-length");
        if (!lenStr.empty()) {
            try { ps.bodyRemaining = stoul(lenStr); }
            catch (...) { onHttpError(fd, 400, "Invalid Content-Length"); return; }
        }

        if (ps.bodyRemaining == 0) {
            dispatchRequest(fd, ps);
        } else {
            ps.stage = Stage::Body;
            // Don't consume from buf here — the outer loop in onRawData
            // will immediately re-enter and call consumeBody()
        }
    }

    // == Body: consume exactly bodyRemaining bytes from buf ====================
    // Returns true when body is complete (request dispatched), false if we need more.
    bool consumeBody(int fd, ParseState& ps, string& buf) {
        size_t take = min(buf.size(), ps.bodyRemaining);
        ps.request.body.append(buf, 0, take);
        buf.erase(0, take);
        ps.bodyRemaining -= take;

        if (ps.bodyRemaining == 0) {
            dispatchRequest(fd, ps);
            return true;
        }
        return false;
    }

    // == Dispatch and reset ====================================================
    void dispatchRequest(int fd, ParseState& ps) {
        HttpRequest req  = ::move(ps.request);
        bool        keep = req.keepAlive();

        ps = ParseState{}; // reset for next request (keep-alive)

        onHttpRequest(fd, req);

        if (!keep) closeAfterFlush(fd);
    }

    // == Helpers ===============================================================
    static string defaultReason(int status) {
        switch (status) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 301: return "Moved Permanently";
            case 302: return "Found";
            case 304: return "Not Modified";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 409: return "Conflict";
            case 500: return "Internal Server Error";
            case 501: return "Not Implemented";
            case 503: return "Service Unavailable";
            default:  return "Unknown";
        }
    }
};

#pragma once

#ifdef TEST

#include "../cpptools/misc/TEST.hpp"
#include "../cpptools/misc/str_contains.hpp"
#include "../HttpServer.hpp"

using namespace std;

// =============================================================================
// HttpRequest Tests
// =============================================================================
TEST(test_HttpRequest_header_returns_header_value) {
    HttpRequest req;
    req.headers["content-type"] = "application/json";
    req.headers["host"] = "localhost:8080";
    
    assert(req.header("content-type") == "application/json");
    assert(req.header("Content-Type") == "application/json"); // case-insensitive
    assert(req.header("HOST") == "localhost:8080");
    assert(req.header("missing", "default") == "default");
}

TEST(test_HttpRequest_keepAlive_http11_default) {
    HttpRequest req;
    req.version = "HTTP/1.1";
    assert(req.keepAlive() == true);
}

TEST(test_HttpRequest_keepAlive_http10_default) {
    HttpRequest req;
    req.version = "HTTP/1.0";
    assert(req.keepAlive() == false);
}

TEST(test_HttpRequest_keepAlive_explicit_close) {
    HttpRequest req;
    req.version = "HTTP/1.1";
    req.headers["connection"] = "close";
    assert(req.keepAlive() == false);
}

TEST(test_HttpRequest_keepAlive_explicit_keepalive) {
    HttpRequest req;
    req.version = "HTTP/1.0";
    req.headers["connection"] = "keep-alive";
    assert(req.keepAlive() == true);
}

// =============================================================================
// HttpResponse Tests
// =============================================================================
TEST(test_HttpResponse_setStatus_sets_status_and_reason) {
    HttpResponse res;
    res.setStatus(404, "Not Found");
    assert(res.status == 404);
    assert(res.reason == "Not Found");
}

TEST(test_HttpResponse_setBody_sets_body_and_headers) {
    HttpResponse res;
    res.setBody("Hello World", "text/html");
    assert(res.body == "Hello World");
    assert(res.headers["Content-Type"] == "text/html");
    assert(res.headers["Content-Length"] == "11");
}

TEST(test_HttpResponse_serialize_produces_valid_http_response) {
    HttpResponse res;
    res.status = 200;
    res.reason = "OK";
    res.setBody("Hello", "text/plain");
    
    string serialized = res.serialize();
    assert(str_contains(serialized, "HTTP/1.1 200 OK"));
    assert(str_contains(serialized, "Content-Type: text/plain"));
    assert(str_contains(serialized, "Content-Length: 5"));
    assert(str_contains(serialized, "\r\n\r\nHello"));
}

TEST(test_HttpResponse_ok_creates_ok_response) {
    HttpResponse res = HttpResponse::ok("test body");
    assert(res.status == 200);
    assert(res.reason == "OK");
    assert(res.body == "test body");
}

TEST(test_HttpResponse_html_creates_html_response) {
    HttpResponse res = HttpResponse::html("<html></html>");
    assert(res.status == 200);
    assert(res.headers["Content-Type"] == "text/html");
}

TEST(test_HttpResponse_json_creates_json_response) {
    HttpResponse res = HttpResponse::json("{\"key\":\"value\"}");
    assert(res.status == 200);
    assert(res.headers["Content-Type"] == "application/json");
}

TEST(test_HttpResponse_notFound_creates_404_response) {
    HttpResponse res = HttpResponse::notFound("Not here");
    assert(res.status == 404);
    assert(res.reason == "Not Found");
}

TEST(test_HttpResponse_badRequest_creates_400_response) {
    HttpResponse res = HttpResponse::badRequest("Invalid");
    assert(res.status == 400);
    assert(res.reason == "Bad Request");
}

TEST(test_HttpResponse_internalError_creates_500_response) {
    HttpResponse res = HttpResponse::internalError("Oops");
    assert(res.status == 500);
    assert(res.reason == "Internal Server Error");
}

TEST(test_HttpResponse_methodNotAllowed_creates_405_response) {
    HttpResponse res = HttpResponse::methodNotAllowed();
    assert(res.status == 405);
    assert(res.reason == "Method Not Allowed");
}

TEST(test_HttpResponse_chain_methods) {
    HttpResponse res;
    res.setStatus(201, "Created").setBody("data", "application/octet-stream");
    assert(res.status == 201);
    assert(res.reason == "Created");
    assert(res.body == "data");
    assert(res.headers["Content-Type"] == "application/octet-stream");
}

// =============================================================================
// HttpResponse defaultReason Tests
// =============================================================================
TEST(test_HttpResponse_default_reason_codes) {
    // Test via serialize which uses defaultReason internally
    HttpResponse res;
    res.status = 200;
    res.reason = "OK";
    res.setBody("");
    string s = res.serialize();
    assert(str_contains(s, "200 OK"));
    
    res.status = 404;
    res.reason = "Not Found";
    s = res.serialize();
    assert(str_contains(s, "404 Not Found"));
    
    res.status = 500;
    res.reason = "Internal Server Error";
    s = res.serialize();
    assert(str_contains(s, "500 Internal Server Error"));
}

#endif

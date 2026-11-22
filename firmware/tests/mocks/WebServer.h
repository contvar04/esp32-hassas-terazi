#ifndef MOCK_WEBSERVER_H
#define MOCK_WEBSERVER_H

#include "Arduino.h"
#include "WiFi.h"
#include <functional>
#include <vector>
#include <map>

enum HTTPMethod {
    HTTP_ANY = 0,
    HTTP_GET = 1,
    HTTP_POST = 2,
    HTTP_PUT = 3,
    HTTP_PATCH = 4,
    HTTP_DELETE = 5,
    HTTP_OPTIONS = 6
};

struct MockHttpResponse {
    int statusCode = 0;
    std::string contentType;
    std::string body;
    bool usedSendP = false;
    std::map<std::string, std::string> headers;
};

class WebServer {
public:
    typedef std::function<void(void)> THandlerFunction;

    struct RouteEntry {
        std::string uri;
        HTTPMethod method;
        THandlerFunction handler;
    };

    int configuredPort = 80;
    int beginCallCount = 0;
    int handleClientCallCount = 0;
    std::vector<RouteEntry> routes;
    THandlerFunction notFoundHandler;

    HTTPMethod currentMethod = HTTP_GET;
    std::string currentUri;
    std::map<std::string, std::string> currentArgs;
    int currentContentLength = 0;
    MockHttpResponse lastResponse;

    WebServer(int port = 80) : configuredPort(port) {}

    void reset() {
        beginCallCount = 0;
        handleClientCallCount = 0;
        routes.clear();
        notFoundHandler = nullptr;
        currentMethod = HTTP_GET;
        currentUri.clear();
        currentArgs.clear();
        currentContentLength = 0;
        lastResponse = MockHttpResponse();
    }

    void begin() {
        beginCallCount++;
    }

    void handleClient() {
        handleClientCallCount++;
    }

    void on(const char* uri, THandlerFunction fn) {
        routes.push_back({ uri ? uri : "", HTTP_ANY, fn });
    }

    void on(const char* uri, HTTPMethod method, THandlerFunction fn) {
        routes.push_back({ uri ? uri : "", method, fn });
    }

    void onNotFound(THandlerFunction fn) {
        notFoundHandler = fn;
    }

    String uri() const { return String(currentUri); }
    HTTPMethod method() const { return currentMethod; }
    int clientContentLength() const { return currentContentLength; }

    bool hasArg(const String& name) const {
        return currentArgs.find(name.std_str()) != currentArgs.end();
    }

    String arg(const String& name) const {
        auto it = currentArgs.find(name.std_str());
        if (it == currentArgs.end()) return String("");
        return String(it->second);
    }

    void sendHeader(const String& name, const String& value, bool = false) {
        lastResponse.headers[name.std_str()] = value.std_str();
    }

    void send(int code, const char* content_type = nullptr, const String& content = String("")) {
        lastResponse.statusCode = code;
        lastResponse.contentType = content_type ? content_type : "";
        lastResponse.body = content.std_str();
        lastResponse.usedSendP = false;
    }

    void send(int code, const char* content_type, const char* content) {
        lastResponse.statusCode = code;
        lastResponse.contentType = content_type ? content_type : "";
        lastResponse.body = content ? content : "";
        lastResponse.usedSendP = false;
    }

    void send_P(int code, PGM_P content_type, PGM_P content) {
        lastResponse.statusCode = code;
        lastResponse.contentType = content_type ? content_type : "";
        lastResponse.body = content ? content : "";
        lastResponse.usedSendP = true;
    }

    void send_P(int code, PGM_P content_type, PGM_P content, size_t contentLength) {
        lastResponse.statusCode = code;
        lastResponse.contentType = content_type ? content_type : "";
        lastResponse.body = content ? std::string(content, contentLength) : "";
        lastResponse.usedSendP = true;
    }

    // Test yardımcısı: kayıtlı rotayı gerçek WebServer gibi çağırır
    MockHttpResponse dispatchRequest(HTTPMethod m, const std::string& path, const std::string& plainBody = "", int explicitContentLength = -1) {
        currentMethod = m;
        currentUri = path;
        currentArgs.clear();
        if (!plainBody.empty()) {
            currentArgs["plain"] = plainBody;
        }
        currentContentLength = (explicitContentLength >= 0) ? explicitContentLength : static_cast<int>(plainBody.size());
        lastResponse = MockHttpResponse();

        for (const auto& r : routes) {
            if (r.uri == path && (r.method == HTTP_ANY || r.method == m)) {
                r.handler();
                return lastResponse;
            }
        }
        if (notFoundHandler) {
            notFoundHandler();
        } else {
            send(404, "text/plain", "Not Found");
        }
        return lastResponse;
    }
};

#endif // MOCK_WEBSERVER_H

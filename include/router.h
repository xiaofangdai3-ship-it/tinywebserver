#ifndef ROUTER_H
#define ROUTER_H

#include <string>
#include <functional>
#include <map>
#include <sstream>

class HttpConn;

struct HttpRequest {
    int method;
    std::string url;
    std::string path;
    std::string query;
    std::map<std::string, std::string> queryParams;
};

struct HttpResponse {
    int statusCode = 200;
    std::string contentType = "text/html";
    std::string body;
    std::map<std::string, std::string> headers;
};

using RouteHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

class Router {
public:
    static Router& getInstance() {
        static Router instance;
        return instance;
    }

    void get(const std::string& path, RouteHandler handler);
    void post(const std::string& path, RouteHandler handler);

    RouteHandler* match(const std::string& path, int method);

    void initBuiltinRoutes();
    void parseQueryString(const std::string& query, std::map<std::string, std::string>& params);

    std::map<std::string, RouteHandler> getRoutes_;
    std::map<std::string, RouteHandler> postRoutes_;

private:
    Router() {}
    ~Router() {}
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;
};

#endif

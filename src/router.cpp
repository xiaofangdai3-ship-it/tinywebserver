#include "../include/router.h"
#include "../include/http_conn.h"
#include "../include/logger.h"
#include "../src/config.cpp"
#include <unistd.h>
#include <sys/resource.h>

void Router::get(const std::string& path, RouteHandler handler) {
    getRoutes_[path] = handler;
    LOG_INFO("Registered GET route: %s", path.c_str());
}

void Router::post(const std::string& path, RouteHandler handler) {
    postRoutes_[path] = handler;
    LOG_INFO("Registered POST route: %s", path.c_str());
}

void Router::parseQueryString(const std::string& query, std::map<std::string, std::string>& params) {
    size_t start = 0;
    while (start < query.size()) {
        size_t eq = query.find('=', start);
        if (eq == std::string::npos) break;
        size_t amp = query.find('&', eq);
        if (amp == std::string::npos) amp = query.size();
        std::string key = query.substr(start, eq - start);
        std::string val = query.substr(eq + 1, amp - eq - 1);
        params[key] = val;
        start = amp + 1;
    }
}

RouteHandler* Router::match(const std::string& path, int method) {

    if(method == 0) {
        auto it = getRoutes_.find(path);
        if(it != getRoutes_.end()) return &it->second;
    } else if(method == 1) {
        auto it = postRoutes_.find(path);
        if(it != postRoutes_.end()) return &it->second;
    }
    return nullptr;
}

void Router::initBuiltinRoutes() {

    get("/stats", [](const HttpRequest& req, HttpResponse& resp) {
        resp.contentType = "application/json";

        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        long memKB = usage.ru_maxrss;

        extern int g_user_count;
        extern int g_total_requests;
        extern int g_total_bytes_sent;
        extern time_t g_start_time;

        time_t uptime = time(nullptr) - g_start_time;

        std::ostringstream json;
        json << "{\n";
        json << "  \"server\": \"TinyWebServer\",\n";
        json << "  \"version\": \"2.0\",\n";
        json << "  \"uptime_seconds\": " << uptime << ",\n";
        json << "  \"active_connections\": " << g_user_count << ",\n";
        json << "  \"total_requests\": " << g_total_requests << ",\n";
        json << "  \"total_bytes_sent\": " << g_total_bytes_sent << ",\n";
        json << "  \"memory_kb\": " << memKB << ",\n";
        json << "  \"features\": [\"epoll\", \"threadpool\", \"router\", \"yaml_config\", \"logger\", \"mmap\"]\n";
        json << "}\n";

        resp.body = json.str();
    });

    get("/hello", [](const HttpRequest& req, HttpResponse& resp) {
        resp.contentType = "application/json";
        std::string name = "World";
        auto it = req.queryParams.find("name");
        if (it != req.queryParams.end()) name = it->second;

        resp.body = "{\"message\": \"Hello, " + name + "!\"}\n";
    });

    get("/time", [](const HttpRequest& req, HttpResponse& resp) {
        resp.contentType = "application/json";
        time_t now = time(nullptr);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        resp.body = std::string("{\"time\": \"") + buf + "\"}\n";
    });

    post("/echo", [](const HttpRequest& req, HttpResponse& resp) {
        resp.contentType = "application/json";
        resp.body = "{\"echo\": \"POST received at " + req.path + "\"}\n";
    });

    LOG_INFO("Builtin routes initialized");
}

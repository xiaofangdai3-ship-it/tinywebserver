#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <cassert>

#include "../include/locker.h"
#include "../include/threadpool.h"
#include "../include/http_conn.h"
#include "../include/router.h"
#include "../include/timer.h"
#include "../include/logger.h"
#include "../src/config.cpp"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000

extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);
extern int modfd(int epollfd, int fd, int ev);

extern int g_user_count;
extern int g_total_requests;
extern long long g_total_bytes_sent;
extern time_t g_start_time;

void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart) sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

void show_error(int connfd, const char* info) {
    LOG_ERROR("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char* argv[]) {
    Config& config = Config::getInstance();
    std::string configFile = "server.yaml";
    if(argc >= 2) configFile = argv[1];
    config.load(configFile);

    int port = config.getInt("port", 8080);
    int threadCount = config.getInt("threads", 8);
    int connTimeout = config.getInt("connection_timeout", 60000);
    std::string logFile = config.getString("log_file", "");
    int logLevel = config.getInt("log_level", 1);
    bool consoleLog = config.getBool("console_log", true);

    Logger::getInstance().init(logFile.c_str(), (LogLevel)logLevel, consoleLog);
    LOG_INFO("=================================");
    LOG_INFO("TinyWebServer v2.0 Starting...");
    LOG_INFO("Port: %d", port);
    LOG_INFO("Threads: %d", threadCount);
    LOG_INFO("Connection timeout: %d ms", connTimeout);
    LOG_INFO("Log file: %s", logFile.empty() ? "none (console only)" : logFile.c_str());

    addsig(SIGPIPE, SIG_IGN);

    Router& router = Router::getInstance();
    router.initBuiltinRoutes();

    ThreadPool<HttpConn>* pool = NULL;
    try {
        pool = new ThreadPool<HttpConn>(threadCount);
    }
    catch(...) {
        LOG_FATAL("Failed to create thread pool");
        return 1;
    }
    LOG_INFO("Thread pool created with %d threads", threadCount);

    HttpConn* users = new HttpConn[MAX_FD];
    assert(users);

    TimerWheel timerWheel;
    int expired_fds[100];

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    int ret = 0;
    ret = bind(listenfd, (struct sockaddr*)&address, sizeof(address));
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);

    addfd(epollfd, listenfd, false);
    HttpConn::m_epollfd = epollfd;

    LOG_INFO("Server listening on port %d", port);
    LOG_INFO("Try: curl http://localhost:%d/stats", port);
    LOG_INFO("Try: curl http://localhost:%d/hello?name=YourName", port);
    LOG_INFO("=================================");

    while(true) {
        int expiredCount = timerWheel.tick(expired_fds, 100);
        for(int i = 0; i < expiredCount; ++i) {
            int fd = expired_fds[i];
            if(fd >= 0 && fd < MAX_FD) {
                LOG_WARN("Connection timeout: fd=%d", fd);
                users[fd].close_conn();
            }
        }

        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, 1000);
        if((number < 0) && (errno != EINTR)) {
            LOG_ERROR("epoll failure");
            break;
        }

        for(int i = 0; i < number; i++) {
            int sockfd = events[i].data.fd;

            if(sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);

                if(connfd < 0) {
                    LOG_ERROR("accept error: errno=%d", errno);
                    continue;
                }

                if(HttpConn::m_user_count >= MAX_FD) {
                    show_error(connfd, "Internal server busy");
                    LOG_ERROR("Server busy, rejected connection fd=%d", connfd);
                    continue;
                }

                users[connfd].init(connfd, client_address);

                TimerNode* timer = timerWheel.addTimer(connfd, connTimeout);
                users[connfd].setTimer(timer);

                LOG_INFO("New connection: fd=%d from %s:%d, total=%d",
                         connfd, inet_ntoa(client_address.sin_addr),
                         ntohs(client_address.sin_port), HttpConn::m_user_count);
            }

            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                TimerNode* timer = users[sockfd].getTimer();
                if(timer) timerWheel.delTimer(timer);
                users[sockfd].close_conn();
                LOG_INFO("Connection closed (error/hup): fd=%d", sockfd);
            }

            else if(events[i].events & EPOLLIN) {
                if(users[sockfd].read()) {

                    TimerNode* timer = users[sockfd].getTimer();
                    if(timer) timerWheel.resetTimer(timer, connTimeout);

                    pool->append(users + sockfd);
                }
                else {
                    TimerNode* timer = users[sockfd].getTimer();
                    if(timer) timerWheel.delTimer(timer);
                    users[sockfd].close_conn();
                    LOG_INFO("Connection closed (read error): fd=%d", sockfd);
                }
            }

            else if(events[i].events & EPOLLOUT) {
                if(!users[sockfd].write()) {
                    TimerNode* timer = users[sockfd].getTimer();
                    if(timer) timerWheel.delTimer(timer);
                    users[sockfd].close_conn();
                    LOG_INFO("Connection closed (write error): fd=%d", sockfd);
                }
            }
        }
    }

    close(epollfd);
    close(listenfd);
    delete[] users;
    delete pool;

    LOG_INFO("Server shutdown");
    return 0;
}

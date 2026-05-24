#include "../include/http_conn.h"
#include "../include/router.h"
#include "../include/logger.h"

const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

const char* doc_root = "/home/dxf/.openclaw/workspace/TinyWebServer/root";

int HttpConn::m_epollfd = -1;
int HttpConn::m_user_count = 0;

int g_user_count = 0;
int g_total_requests = 0;
long long g_total_bytes_sent = 0;
time_t g_start_time = time(nullptr);

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if(one_shot) event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

void HttpConn::init(int sockfd, const sockaddr_in& addr) {
    m_sockfd = sockfd;
    m_address = addr;
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epollfd, sockfd, true);
    m_user_count++;
    g_user_count = m_user_count;
    init();
}

void HttpConn::init() {
    memset(read_buf, '\0', READ_BUFFER_SIZE);
    memset(write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
    read_idx = 0; checked_idx = 0; start_line = 0; write_idx = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET;
    m_url = 0; m_version = 0; m_host = 0;
    m_content_length = 0; m_linger = false;
    m_file_address = 0; m_iv_count = 0;
    bytes_to_send = 0; bytes_have_send = 0;
    is_dynamic_ = false;
    dynamic_body_.clear();
    dynamic_type_ = "text/html";
}

void HttpConn::close_conn() {
    if(m_sockfd != -1) {
        LOG_INFO("Connection closed: fd=%d, client=%s:%d", m_sockfd,
                 inet_ntoa(m_address.sin_addr), ntohs(m_address.sin_port));
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
        g_user_count = m_user_count;
    }
}

bool HttpConn::read() {
    if(read_idx >= READ_BUFFER_SIZE) return false;
    int bytes_read = 0;
    while(true) {
        bytes_read = recv(m_sockfd, read_buf + read_idx, READ_BUFFER_SIZE - read_idx, 0);
        if(bytes_read == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) break;
            return false;
        }
        else if(bytes_read == 0) return false;
        read_idx += bytes_read;
    }
    return true;
}

HttpConn::LINE_STATUS HttpConn::parse_line() {
    char temp;
    for(; checked_idx < read_idx; ++checked_idx) {
        temp = read_buf[checked_idx];
        if(temp == '\r') {
            if((checked_idx + 1) == read_idx) return LINE_OPEN;
            else if(read_buf[checked_idx + 1] == '\n') {
                read_buf[checked_idx++] = '\0';
                read_buf[checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp == '\n') {
            if((checked_idx > 1) && (read_buf[checked_idx - 1] == '\r')) {
                read_buf[checked_idx - 1] = '\0';
                read_buf[checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

HttpConn::HTTP_CODE HttpConn::parse_request_line(char* text) {
    m_url = strpbrk(text, " \t");
    if(!m_url) return BAD_REQUEST;
    *m_url++ = '\0';

    char* method = text;
    if(strcasecmp(method, "GET") == 0) m_method = GET;
    else if(strcasecmp(method, "POST") == 0) m_method = POST;
    else return BAD_REQUEST;

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if(!m_version) return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");

    if(strcasecmp(m_version, "HTTP/1.1") != 0) return BAD_REQUEST;
    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(!m_url || m_url[0] != '/') return BAD_REQUEST;

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parse_headers(char* text) {
    if(text[0] == '\0') {
        if(m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if(strncasecmp(text, "Connection:", 11) == 0) {
        text += 11; text += strspn(text, " \t");
        if(strcasecmp(text, "keep-alive") == 0) m_linger = true;
    }
    else if(strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15; text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if(strncasecmp(text, "Host:", 5) == 0) {
        text += 5; text += strspn(text, " \t");
        m_host = text;
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parse_content(char* text) {
    if(read_idx >= (m_content_length + checked_idx)) {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::do_request() {

    Router& router = Router::getInstance();

    std::string reqPath = m_url;
    size_t qpos = reqPath.find('?');
    if(qpos != std::string::npos) reqPath = reqPath.substr(0, qpos);

    RouteHandler* handler = router.match(reqPath, m_method);

    if(handler) {
        HttpRequest req;
        req.method = m_method;
        req.url = m_url;
        req.path = reqPath;

        std::string fullUrl(m_url);
        size_t qp = fullUrl.find('?');
        if(qp != std::string::npos) {
            req.query = fullUrl.substr(qp + 1);
            router.parseQueryString(req.query, req.queryParams);
        }

        HttpResponse resp;
        (*handler)(req, resp);

        is_dynamic_ = true;
        dynamic_body_ = resp.body;
        dynamic_type_ = resp.contentType;

        LOG_INFO("Dynamic route: %s -> %s (status=%d, size=%zu)",
                 req.path.c_str(), resp.contentType.c_str(),
                 resp.statusCode, resp.body.size());

        return FILE_REQUEST;
    }

    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if(m_url[strlen(m_url) - 1] == '/') {
        strncat(m_real_file, "index.html", FILENAME_LEN - strlen(m_real_file) - 1);
    }

    if(stat(m_real_file, &m_file_stat) < 0) return NO_RESOURCE;
    if(!(m_file_stat.st_mode & S_IROTH)) return FORBIDDEN_REQUEST;
    if(S_ISDIR(m_file_stat.st_mode)) return BAD_REQUEST;

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    is_dynamic_ = false;
    return FILE_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while(((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
          || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        start_line = checked_idx;

        switch(m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                else if(ret == GET_REQUEST) return do_request();
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if(ret == GET_REQUEST) return do_request();
                line_status = LINE_OPEN;
                break;
            }
            default: return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

void HttpConn::unmap() {
    if(m_file_address && !is_dynamic_) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool HttpConn::write() {
    int temp = 0;

    if(bytes_to_send == 0) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while(true) {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if(temp < 0) {
            if(errno == EAGAIN) {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        g_total_bytes_sent += temp;

        if(bytes_have_send >= (int)m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = (is_dynamic_ ? (char*)dynamic_body_.c_str() : m_file_address)
                               + (bytes_have_send - write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else {
            m_iv[0].iov_base = write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if(bytes_to_send <= 0) {
            unmap();
            if(m_linger) {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            else {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

bool HttpConn::add_response(const char* format, ...) {
    if(write_idx >= WRITE_BUFFER_SIZE) return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(write_buf + write_idx, WRITE_BUFFER_SIZE - 1 - write_idx, format, arg_list);
    if(len >= (WRITE_BUFFER_SIZE - 1 - write_idx)) {
        va_end(arg_list);
        return false;
    }
    write_idx += len;
    va_end(arg_list);
    return true;
}

bool HttpConn::add_content(const char* content) {
    return add_response("%s", content);
}

bool HttpConn::add_status_line(int status, const char* title) {
    return add_response("HTTP/1.1 %d %s\r\n", status, title);
}

bool HttpConn::add_content_type(const char* type) {
    return add_response("Content-Type: %s\r\n", type);
}

bool HttpConn::add_headers(int content_length) {
    add_content_length(content_length);
    add_linger();
    add_blank_line();
    return true;
}

bool HttpConn::add_content_length(int content_length) {
    return add_response("Content-Length: %d\r\n", content_length);
}

bool HttpConn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool HttpConn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool HttpConn::process_write(HTTP_CODE ret) {
    switch(ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)) return false;
            break;
        }
        case BAD_REQUEST: {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if(!add_content(error_400_form)) return false;
            break;
        }
        case NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if(!add_content(error_404_form)) return false;
            break;
        }
        case FORBIDDEN_REQUEST: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) return false;
            break;
        }
        case FILE_REQUEST: {
            add_status_line(200, ok_200_title);

            if(is_dynamic_) {

                add_content_type(dynamic_type_.c_str());
                add_content_length(dynamic_body_.size());
                add_linger();
                add_blank_line();

                m_iv[0].iov_base = write_buf;
                m_iv[0].iov_len = write_idx;
                m_iv[1].iov_base = (char*)dynamic_body_.c_str();
                m_iv[1].iov_len = dynamic_body_.size();
                m_iv_count = 2;
                bytes_to_send = write_idx + dynamic_body_.size();
                return true;
            }

            if(m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = write_buf;
                m_iv[0].iov_len = write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = write_idx + m_file_stat.st_size;
                return true;
            }
            else {
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)) return false;
            }
            break;
        }
        default: return false;
    }

    m_iv[0].iov_base = write_buf;
    m_iv[0].iov_len = write_idx;
    m_iv_count = 1;
    bytes_to_send = write_idx;
    return true;
}

void HttpConn::process() {
    g_total_requests++;
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST) {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
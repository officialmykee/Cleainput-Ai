#include "http.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace cleainput {

// Simple HTTP response
struct HttpResp {
    int         status = 0;
    std::string body;
    std::unordered_map<
        std::string,
        std::string> headers;
};

// Parse URL into parts
struct URL {
    std::string scheme;
    std::string host;
    uint16_t    port = 80;
    std::string path;
    std::string query;

    static URL parse(
        const std::string& url) {
        URL u;
        std::string s = url;

        // Scheme
        size_t p = s.find("://");
        if (p != std::string::npos) {
            u.scheme = s.substr(0, p);
            s = s.substr(p + 3);
        }

        if (u.scheme == "https")
            u.port = 443;

        // Path
        size_t slash = s.find('/');
        if (slash != std::string::npos) {
            u.path = s.substr(slash);
            s = s.substr(0, slash);
        } else {
            u.path = "/";
        }

        // Query
        size_t q = u.path.find('?');
        if (q != std::string::npos) {
            u.query = u.path.substr(q+1);
            u.path  = u.path.substr(0,q);
        }

        // Port
        size_t colon = s.find(':');
        if (colon != std::string::npos) {
            u.port = (uint16_t)std::stoi(
                s.substr(colon + 1));
            u.host = s.substr(0, colon);
        } else {
            u.host = s;
        }

        return u;
    }
};

// Connect to host
static int connect_to(
    const std::string& host,
    uint16_t port) {

    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    std::string port_str =
        std::to_string(port);

    int rc = getaddrinfo(
        host.c_str(),
        port_str.c_str(),
        &hints, &res);

    if (rc != 0 || !res) return -1;

    int fd = socket(
        res->ai_family,
        res->ai_socktype,
        res->ai_protocol);

    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    // Set timeout 10s
    struct timeval tv{10, 0};
    setsockopt(fd, SOL_SOCKET,
        SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET,
        SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd,
            res->ai_addr,
            res->ai_addrlen) < 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return fd;
}

// Build HTTP request string
static std::string build_request(
    const std::string& method,
    const URL& url,
    const std::string& body,
    const std::unordered_map<
        std::string,
        std::string>& headers) {

    std::ostringstream req;
    req << method << " "
        << url.path;
    if (!url.query.empty())
        req << "?" << url.query;
    req << " HTTP/1.1\r\n";
    req << "Host: " << url.host << "\r\n";
    req << "Connection: close\r\n";
    req << "User-Agent: "
           "Cleainput-AI/1.0\r\n";

    for (auto& [k, v] : headers)
        req << k << ": " << v << "\r\n";

    if (!body.empty()) {
        req << "Content-Length: "
            << body.size() << "\r\n";
        if (headers.find("Content-Type")
                == headers.end())
            req << "Content-Type: "
                   "application/json\r\n";
    }

    req << "\r\n";
    if (!body.empty())
        req << body;

    return req.str();
}

// Parse HTTP response
static HttpResp parse_response(int fd) {
    HttpResp resp;
    std::string raw;
    char buf[4096];
    ssize_t n;

    while ((n = recv(fd, buf,
                     sizeof(buf)-1,
                     0)) > 0) {
        buf[n] = '\0';
        raw += buf;
    }

    if (raw.empty()) return resp;

    // Status line
    size_t p = raw.find("\r\n");
    if (p == std::string::npos)
        return resp;

    std::string status_line =
        raw.substr(0, p);
    size_t sp1 = status_line.find(' ');
    size_t sp2 = status_line.find(
        ' ', sp1+1);
    if (sp1 != std::string::npos)
        resp.status = std::stoi(
            status_line.substr(
                sp1+1, sp2-sp1-1));

    // Headers + body
    size_t hend = raw.find("\r\n\r\n");
    if (hend == std::string::npos)
        return resp;

    std::string header_block =
        raw.substr(p+2, hend-p-2);
    resp.body = raw.substr(hend+4);

    // Parse headers
    std::istringstream hs(header_block);
    std::string line;
    while (std::getline(hs, line)) {
        if (line.empty() ||
            line == "\r") continue;
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string key =
            line.substr(0, colon);
        std::string val =
            line.substr(colon+2);
        if (!val.empty() &&
            val.back() == '\r')
            val.pop_back();
        resp.headers[key] = val;
    }

    return resp;
}

// HTTP GET
std::string http_get(
    const std::string& url_str,
    const std::unordered_map<
        std::string,
        std::string>& headers) {

    URL url = URL::parse(url_str);
    int fd  = connect_to(
        url.host, url.port);
    if (fd < 0) {
        std::cerr << "[HTTP] Connect "
                     "failed: "
                  << url.host << "\n";
        return "";
    }

    std::string req = build_request(
        "GET", url, "", headers);
    send(fd, req.c_str(),
         req.size(), 0);

    HttpResp resp = parse_response(fd);
    close(fd);

    if (resp.status != 200) {
        std::cerr << "[HTTP] GET "
                  << resp.status
                  << " " << url_str
                  << "\n";
    }

    return resp.body;
}

// HTTP POST
std::string http_post(
    const std::string& url_str,
    const std::string& body,
    const std::unordered_map<
        std::string,
        std::string>& headers) {

    URL url = URL::parse(url_str);
    int fd  = connect_to(
        url.host, url.port);
    if (fd < 0) {
        std::cerr << "[HTTP] Connect "
                     "failed: "
                  << url.host << "\n";
        return "";
    }

    std::string req = build_request(
        "POST", url, body, headers);
    send(fd, req.c_str(),
         req.size(), 0);

    HttpResp resp = parse_response(fd);
    close(fd);

    if (resp.status < 200 ||
        resp.status >= 300) {
        std::cerr << "[HTTP] POST "
                  << resp.status
                  << " " << url_str
                  << "\n";
    }

    return resp.body;
}

// HTTP PATCH
std::string http_patch(
    const std::string& url_str,
    const std::string& body,
    const std::unordered_map<
        std::string,
        std::string>& headers) {

    URL url = URL::parse(url_str);
    int fd  = connect_to(
        url.host, url.port);
    if (fd < 0) return "";

    std::string req = build_request(
        "PATCH", url, body, headers);
    send(fd, req.c_str(),
         req.size(), 0);

    HttpResp resp = parse_response(fd);
    close(fd);
    return resp.body;
}

// HTTP DELETE
bool http_delete(
    const std::string& url_str,
    const std::unordered_map<
        std::string,
        std::string>& headers) {

    URL url = URL::parse(url_str);
    int fd  = connect_to(
        url.host, url.port);
    if (fd < 0) return false;

    std::string req = build_request(
        "DELETE", url, "", headers);
    send(fd, req.c_str(),
         req.size(), 0);

    HttpResp resp = parse_response(fd);
    close(fd);
    return resp.status >= 200
        && resp.status < 300;
}

} // namespace cleainput

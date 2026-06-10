#include "fetcher.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <regex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>

namespace cleainput {

// Robots.txt cache
static std::unordered_map<
    std::string,
    std::string> robots_cache_;

// Check robots.txt
static bool allowed_by_robots(
    const std::string& host,
    const std::string& path) {

    auto it = robots_cache_.find(host);
    if (it == robots_cache_.end()) {
        // Fetch robots.txt
        std::string robots_url =
            "http://" + host
            + "/robots.txt";
        // Simple check — full impl
        // would parse properly
        robots_cache_[host] = "";
        return true;
    }

    // Check if path is disallowed
    const std::string& robots =
        it->second;
    if (robots.find("Disallow: "
        + path) != std::string::npos)
        return false;

    return true;
}

// Rate limiter per domain
static std::unordered_map<
    std::string,
    uint64_t> last_fetch_;

static bool rate_limit_ok(
    const std::string& host,
    uint32_t delay_ms = 1000) {

    auto now = std::chrono::duration_cast<
        std::chrono::milliseconds>(
        std::chrono::system_clock
            ::now().time_since_epoch())
        .count();

    auto it = last_fetch_.find(host);
    if (it != last_fetch_.end()) {
        if (now - it->second < delay_ms)
            return false;
    }

    last_fetch_[host] = now;
    return true;
}

// Parse URL
static void parse_url(
    const std::string& url,
    std::string& scheme,
    std::string& host,
    uint16_t& port,
    std::string& path) {

    scheme = "http";
    port   = 80;
    path   = "/";

    std::string s = url;

    // Scheme
    size_t p = s.find("://");
    if (p != std::string::npos) {
        scheme = s.substr(0, p);
        s = s.substr(p + 3);
        if (scheme == "https")
            port = 443;
    }

    // Path
    size_t slash = s.find('/');
    if (slash != std::string::npos) {
        path = s.substr(slash);
        s    = s.substr(0, slash);
    }

    // Port
    size_t colon = s.find(':');
    if (colon != std::string::npos) {
        port = (uint16_t)std::stoi(
            s.substr(colon + 1));
        host = s.substr(0, colon);
    } else {
        host = s;
    }
}

// Connect to host
static int tcp_connect(
    const std::string& host,
    uint16_t port) {

    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = nullptr;
    std::string ps = std::to_string(port);

    if (getaddrinfo(host.c_str(),
            ps.c_str(),
            &hints, &res) != 0)
        return -1;

    int fd = socket(
        res->ai_family,
        res->ai_socktype,
        res->ai_protocol);

    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    // 10 second timeout
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

// Fetch a URL
FetchResult Fetcher::fetch(
    const std::string& url,
    uint32_t timeout_ms) {

    FetchResult result;
    result.url = url;
    result.success = false;

    std::string scheme, host, path;
    uint16_t port;
    parse_url(url, scheme, host,
              port, path);

    // Check robots.txt
    if (!allowed_by_robots(host, path)) {
        result.error =
            "Blocked by robots.txt";
        return result;
    }

    // Rate limiting
    if (!rate_limit_ok(host, 1000)) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(
                1000));
    }

    // Connect
    int fd = tcp_connect(host, port);
    if (fd < 0) {
        result.error =
            "Connection failed: " + host;
        return result;
    }

    // Build HTTP request
    std::string req =
        "GET " + path +
        " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "User-Agent: Cleainput-AI "
        "Crawler/1.0\r\n"
        "Accept: text/html,"
        "application/json\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n"
        "\r\n";

    send(fd, req.c_str(),
         req.size(), 0);

    // Read response
    std::string raw;
    char buf[8192];
    ssize_t n;
    size_t max_size =
        5 * 1024 * 1024; // 5MB max

    while ((n = recv(fd, buf,
                     sizeof(buf)-1,
                     0)) > 0) {
        buf[n] = '\0';
        raw += buf;
        if (raw.size() > max_size)
            break;
    }
    close(fd);

    if (raw.empty()) {
        result.error = "Empty response";
        return result;
    }

    // Parse status
    size_t p = raw.find("\r\n");
    if (p != std::string::npos) {
        std::string sl = raw.substr(0,p);
        size_t s1 = sl.find(' ');
        size_t s2 = sl.find(' ', s1+1);
        if (s1 != std::string::npos)
            result.status_code =
                std::stoi(sl.substr(
                    s1+1, s2-s1-1));
    }

    // Handle redirects
    if (result.status_code == 301 ||
        result.status_code == 302) {
        size_t loc = raw.find(
            "Location: ");
        if (loc != std::string::npos) {
            size_t end = raw.find(
                "\r\n", loc);
            result.redirect_url =
                raw.substr(loc+10,
                    end-loc-10);
        }
        result.error = "Redirect";
        return result;
    }

    // Extract body
    size_t hend = raw.find("\r\n\r\n");
    if (hend != std::string::npos) {
        result.html = raw.substr(
            hend + 4);
    }

    // Extract title
    std::regex title_re(
        "<title[^>]*>([^<]+)</title>",
        std::regex::icase);
    std::smatch m;
    if (std::regex_search(
            result.html, m, title_re))
        result.title = m[1].str();

    // Extract links
    std::regex link_re(
        "href=[\"']([^\"']+)[\"']",
        std::regex::icase);
    auto begin = std::sregex_iterator(
        result.html.begin(),
        result.html.end(),
        link_re);
    auto end = std::sregex_iterator();

    for (auto i = begin; i != end; ++i) {
        std::string link = (*i)[1].str();
        // Normalize relative URLs
        if (link.substr(0,4) != "http") {
            if (link[0] == '/')
                link = scheme + "://"
                     + host + link;
            else
                link = scheme + "://"
                     + host + "/"
                     + link;
        }
        result.links.push_back(link);
    }

    result.success =
        result.status_code == 200;
    result.fetched_at =
        std::chrono::duration_cast<
            std::chrono::seconds>(
            std::chrono::system_clock
                ::now().time_since_epoch())
        .count();

    stats_.pages_fetched++;
    stats_.bytes_downloaded +=
        result.html.size();

    return result;
}

// Crawl multiple pages
std::vector<FetchResult>
Fetcher::crawl(
    const std::string& seed_url,
    uint32_t max_depth,
    uint32_t max_pages) {

    std::vector<FetchResult> results;
    std::vector<std::string> queue
        = {seed_url};
    std::unordered_map<
        std::string, bool> visited;

    uint32_t depth = 0;

    while (!queue.empty()
        && results.size() < max_pages
        && depth < max_depth) {

        std::vector<std::string>
            next_queue;

        for (auto& url : queue) {
            if (visited[url]) continue;
            if (results.size()
                    >= max_pages) break;

            visited[url] = true;

            std::cout << "[CRAWL] "
                      << url << "\n";

            FetchResult r = fetch(url);
            if (r.success) {
                results.push_back(r);
                // Add links to next level
                for (auto& link
                        : r.links) {
                    if (!visited[link])
                        next_queue
                            .push_back(
                                link);
                }
            }

            // Be polite — wait 1s
            std::this_thread::sleep_for(
                std::chrono::
                    milliseconds(1000));
        }

        queue = next_queue;
        depth++;
    }

    std::cout << "[CRAWL] Done: "
              << results.size()
              << " pages\n";

    return results;
}

} // namespace cleainput

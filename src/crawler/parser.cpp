#include "parser.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <regex>
#include <cctype>

namespace cleainput {

// Tags to completely remove
// with their content
static const std::vector<std::string>
REMOVE_TAGS = {
    "script", "style", "noscript",
    "iframe", "svg", "canvas",
    "video",  "audio", "figure",
    "nav",    "footer", "header",
    "aside",  "ads",   "form"
};

// Tags that add newlines
static const std::vector<std::string>
BLOCK_TAGS = {
    "p",  "div", "br",  "h1",
    "h2", "h3",  "h4",  "h5",
    "h6", "li",  "tr",  "td",
    "th", "blockquote", "pre"
};

// Remove HTML tag and content
static std::string remove_tag_content(
    const std::string& html,
    const std::string& tag) {

    std::string result = html;
    std::string open  = "<" + tag;
    std::string close = "</" + tag + ">";

    size_t start = 0;
    while (true) {
        // Case insensitive search
        std::string lower = result;
        std::transform(
            lower.begin(),
            lower.end(),
            lower.begin(),
            ::tolower);

        size_t s = lower.find(
            open, start);
        if (s == std::string::npos)
            break;

        size_t e = lower.find(
            close, s);
        if (e == std::string::npos) {
            // No close tag — remove
            // to end of tag at least
            size_t te = lower.find(
                '>', s);
            if (te != std::string::npos)
                result.erase(s, te-s+1);
            break;
        }

        result.erase(s,
            e + close.size() - s);
    }

    return result;
}

// Replace block tags with newlines
static std::string block_to_newlines(
    const std::string& html) {

    std::string result = html;
    for (auto& tag : BLOCK_TAGS) {
        std::string open  = "<" + tag;
        std::string close = "</"
            + tag + ">";

        // Replace opening tags
        size_t p = 0;
        while (true) {
            std::string lower = result;
            std::transform(
                lower.begin(),
                lower.end(),
                lower.begin(),
                ::tolower);
            p = lower.find(open, p);
            if (p == std::string::npos)
                break;
            size_t end = lower.find(
                '>', p);
            if (end == std::string::npos)
                break;
            result.replace(
                p, end-p+1, "\n");
            p++;
        }

        // Replace closing tags
        p = 0;
        while (true) {
            std::string lower = result;
            std::transform(
                lower.begin(),
                lower.end(),
                lower.begin(),
                ::tolower);
            p = lower.find(close, p);
            if (p == std::string::npos)
                break;
            result.replace(
                p, close.size(), "\n");
            p++;
        }
    }
    return result;
}

// Strip all remaining HTML tags
static std::string strip_tags(
    const std::string& html) {

    std::string result;
    bool in_tag = false;

    for (size_t i = 0;
         i < html.size(); i++) {
        char c = html[i];
        if (c == '<') {
            in_tag = true;
        } else if (c == '>') {
            in_tag = false;
        } else if (!in_tag) {
            result += c;
        }
    }
    return result;
}

// Decode HTML entities
static std::string decode_entities(
    const std::string& text) {

    std::string result = text;

    struct Entity {
        const char* encoded;
        const char* decoded;
    };

    static const Entity entities[] = {
        {"&amp;",   "&"},
        {"&lt;",    "<"},
        {"&gt;",    ">"},
        {"&quot;",  "\""},
        {"&apos;",  "'"},
        {"&nbsp;",  " "},
        {"&mdash;", "—"},
        {"&ndash;", "–"},
        {"&lsquo;", "'"},
        {"&rsquo;", "'"},
        {"&ldquo;", "\""},
        {"&rdquo;", "\""},
        {"&#39;",   "'"},
        {"&#160;",  " "},
        {nullptr,   nullptr}
    };

    for (int i = 0;
         entities[i].encoded; i++) {
        std::string enc =
            entities[i].encoded;
        std::string dec =
            entities[i].decoded;
        size_t p = 0;
        while ((p = result.find(
                enc, p))
                != std::string::npos) {
            result.replace(
                p, enc.size(), dec);
            p += dec.size();
        }
    }

    return result;
}

// Normalize whitespace
static std::string normalize_whitespace(
    const std::string& text) {

    std::string result;
    bool last_was_space = false;
    bool last_was_newline = false;
    int  newline_count = 0;

    for (char c : text) {
        if (c == '\n' || c == '\r') {
            newline_count++;
            if (newline_count <= 2) {
                result += '\n';
            }
            last_was_space   = false;
            last_was_newline = true;
        } else if (c == ' ' ||
                   c == '\t') {
            if (!last_was_space &&
                !last_was_newline) {
                result += ' ';
                last_was_space = true;
            }
        } else {
            result += c;
            last_was_space   = false;
            last_was_newline = false;
            newline_count    = 0;
        }
    }

    return result;
}

// Extract meta description
static std::string extract_meta(
    const std::string& html,
    const std::string& name) {

    std::regex meta_re(
        "<meta[^>]+name=[\"']"
        + name +
        "[\"'][^>]+content=[\"']"
        "([^\"']+)[\"']",
        std::regex::icase);

    std::smatch m;
    if (std::regex_search(
            html, m, meta_re))
        return m[1].str();

    // Try reversed attribute order
    std::regex meta_re2(
        "<meta[^>]+content=[\"']"
        "([^\"']+)[\"'][^>]+name=[\"']"
        + name + "[\"']",
        std::regex::icase);

    if (std::regex_search(
            html, m, meta_re2))
        return m[1].str();

    return "";
}

// Main parse function
ParseResult Parser::parse(
    const std::string& html,
    const std::string& url) {

    ParseResult result;
    result.url = url;

    if (html.empty()) {
        result.success = false;
        return result;
    }

    std::string text = html;

    // Extract title
    std::regex title_re(
        "<title[^>]*>([^<]+)</title>",
        std::regex::icase);
    std::smatch tm;
    if (std::regex_search(
            text, tm, title_re))
        result.title = tm[1].str();

    // Extract meta description
    result.description =
        extract_meta(html, "description");

    // Extract keywords
    std::string kw =
        extract_meta(html, "keywords");
    if (!kw.empty()) {
        std::istringstream ss(kw);
        std::string k;
        while (std::getline(ss, k, ',')) {
            // Trim
            k.erase(0,
                k.find_first_not_of(" "));
            k.erase(
                k.find_last_not_of(" ")+1);
            if (!k.empty())
                result.keywords
                    .push_back(k);
        }
    }

    // Extract links
    std::regex link_re(
        "href=[\"']([^\"'#]+)[\"']",
        std::regex::icase);
    auto lb = std::sregex_iterator(
        text.begin(), text.end(),
        link_re);
    auto le = std::sregex_iterator();
    for (auto i = lb; i != le; ++i) {
        std::string link = (*i)[1].str();
        if (link.substr(0,4) == "http")
            result.links.push_back(link);
    }

    // Remove unwanted tags
    for (auto& tag : REMOVE_TAGS)
        text = remove_tag_content(
            text, tag);

    // Block tags → newlines
    text = block_to_newlines(text);

    // Strip remaining tags
    text = strip_tags(text);

    // Decode HTML entities
    text = decode_entities(text);

    // Normalize whitespace
    text = normalize_whitespace(text);

    // Trim leading/trailing
    size_t start =
        text.find_first_not_of(
            " \n\t");
    size_t end =
        text.find_last_not_of(
            " \n\t");

    if (start != std::string::npos)
        text = text.substr(
            start, end-start+1);

    result.clean_text = text;

    // Word count
    std::istringstream wss(text);
    std::string word;
    while (wss >> word)
        result.word_count++;

    // Reading time (200 wpm)
    result.reading_time_sec =
        (result.word_count / 200) * 60;

    // Language detection (simple)
    result.language = "en";

    result.success = !text.empty();
    result.parsed_at =
        std::chrono::duration_cast<
            std::chrono::seconds>(
            std::chrono::system_clock
                ::now().time_since_epoch())
        .count();

    stats_.pages_parsed++;
    stats_.words_extracted +=
        result.word_count;

    std::cout << "[PARSER] "
              << result.title
              << " — "
              << result.word_count
              << " words\n";

    return result;
}

// Parse multiple pages
std::vector<ParseResult>
Parser::parse_all(
    const std::vector<FetchResult>&
        pages) {

    std::vector<ParseResult> results;
    results.reserve(pages.size());

    for (auto& page : pages) {
        if (!page.success) continue;
        auto r = parse(
            page.html, page.url);
        if (r.success)
            results.push_back(r);
    }

    std::cout << "[PARSER] Parsed "
              << results.size()
              << " pages\n";

    return results;
}

// Extract main content only
// Removes boilerplate/nav/ads
std::string Parser::extract_main(
    const std::string& html) {

    // Look for main content tags
    std::vector<std::string> main_tags = {
        "main", "article",
        "content", "post-content",
        "entry-content", "post-body"
    };

    for (auto& tag : main_tags) {
        std::regex re(
            "<(?:div|section|article)"
            "[^>]+(?:id|class)=[\"'][^\"']*"
            + tag +
            "[^\"']*[\"'][^>]*>"
            "([\\s\\S]*?)"
            "</(?:div|section|article)>",
            std::regex::icase);
        std::smatch m;
        if (std::regex_search(
                html, m, re))
            return m[1].str();
    }

    // Fallback — return full body
    std::regex body_re(
        "<body[^>]*>([\\s\\S]*?)</body>",
        std::regex::icase);
    std::smatch bm;
    if (std::regex_search(
            html, bm, body_re))
        return bm[1].str();

    return html;
}

// Chunk text for RAG
std::vector<TextChunk>
Parser::chunk_text(
    const std::string& text,
    size_t chunk_size,
    size_t overlap) {

    std::vector<TextChunk> chunks;

    if (text.empty())
        return chunks;

    // Split into sentences first
    std::vector<std::string> sentences;
    std::string current;

    for (size_t i = 0;
         i < text.size(); i++) {
        current += text[i];
        if ((text[i] == '.' ||
             text[i] == '!' ||
             text[i] == '?') &&
            i+1 < text.size() &&
            text[i+1] == ' ') {
            sentences.push_back(current);
            current.clear();
        }
    }
    if (!current.empty())
        sentences.push_back(current);

    // Group sentences into chunks
    std::string chunk;
    size_t chunk_idx = 0;

    for (size_t i = 0;
         i < sentences.size(); i++) {
        chunk += sentences[i];

        if (chunk.size() >= chunk_size ||
            i == sentences.size()-1) {

            TextChunk tc;
            tc.text      = chunk;
            tc.chunk_idx = chunk_idx++;
            tc.start_pos = i;
            tc.word_count =
                std::count_if(
                    chunk.begin(),
                    chunk.end(),
                    [](char c) {
                        return c == ' ';
                    }) + 1;
            chunks.push_back(tc);

            // Overlap — keep last
            // few sentences
            if (overlap > 0 &&
                i + 1 <
                sentences.size()) {
                chunk.clear();
                size_t start = i >= overlap
                    ? i - overlap + 1
                    : 0;
                for (size_t j = start;
                     j <= i; j++)
                    chunk +=
                        sentences[j];
            } else {
                chunk.clear();
            }
        }
    }

    std::cout << "[PARSER] Chunked: "
              << chunks.size()
              << " chunks\n";

    return chunks;
}

} // namespace cleainput

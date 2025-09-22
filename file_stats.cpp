#include <algorithm>
#include <cctype>
#include <chrono>
#include <clocale>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Holds CLI configuration parsed from command-line arguments.
struct Config
{
    std::string input_path;   // Path to the input text file to analyze
    std::string json_path;    // If non-empty, write JSON report to this path
    std::size_t topN = 20;    // Number of top frequent words to display/export
    bool case_sensitive = false; // Word frequency counting mode
};

// Print short help/usage instructions.
static void print_help(const char* exe) {
    std::cout   << "File Stats - Text file analysis\n\n"
                << "Usage:\n"
                << "  " << exe << " <input.txt> [--top N] [--json out.json] [--case-sensitive] [--help]\n\n"
                << "Options:\n"
                << "  --top N            Show top N most frequent words (default: 20)\n"
                << "  --json out.json    Export results to JSON file\n"
                << "  --case-sensitive   Word frequency is case-sensitive (default: false)\n"
                << "  --help             Show this help and exit\n";
}

// Parse command-line args into Config. Returns false if args are invalid.
static bool parse_args(int argc, char** argv, Config& conf) {
    if (argc < 2) return false;              // Require at least an input file
    conf.input_path = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string a =  argv[i];
        if (a == "--help" || a == "-h") {
            print_help(argv[0]);
            std::exit(0);                    // Early exit after showing help
        } else if (a == "--top" && i + 1 < argc) {
            conf.topN = static_cast<std::size_t>(std::stoul(argv[++i]));
        } else if (a == "--json" && i + 1 < argc) {
            conf.json_path = argv[++i];
        } else if (a == "--case-sensitive") {
            conf.case_sensitive = true;
        } else {
            std::cerr << "Unknown argument: " << a << "\n";
            return false;
        }
    }

    return true;
}

// ASCII-safe predicate: a "word char" is alphanumeric (A–Z, a–z, 0–9).
// This intentionally ignores accents and non-ASCII letters to avoid locale issues.
static inline bool is_word_char(unsigned char ch) {
    return std::isalnum(ch) != 0;
}

// Aggregated statistics produced by the analyzer.
struct Stats
{
    std::uint64_t lines = 0;  // Number of lines in the file
    std::uint64_t words = 0;  // Number of words detected by is_word_char tokenization
    std::uint64_t bytes = 0;  // File size in bytes (octets)
    // Word frequency map: token -> count
    std::unordered_map<std::string, std::uint64_t> freq;
};

// Read the file, count lines/words, build frequency table, and compute byte size.
static Stats analyze_file(const Config& conf) {
    Stats st;

    // Open text file in default (text) mode.
    std::ifstream in(conf.input_path);
    if (!in) {
        throw std::runtime_error("Cannot open input file: " + conf.input_path);
    }

    std::string line;
    // Read line by line to count lines and tokenize content into words.
    while (std::getline(in, line)) {
        ++st.lines;

        // Access raw bytes to avoid signed-char UB and to keep ASCII logic explicit.
        const unsigned char* p = reinterpret_cast<const unsigned char*>(line.data());
        std::size_t n = line.size();
        std::string token;            // Current word being built
        token.reserve(32);            // Small optimization: reduce reallocations

        for (std::size_t i = 0; i < n; ++i) {
            unsigned char ch = p[i];
            if (is_word_char(ch)) {
                // Build token; optionally lowercase for case-insensitive mode.
                char c = static_cast<char>(ch);
                if (!conf.case_sensitive) c = static_cast<char>(std::tolower(c));
                token.push_back(c);
            } else {
                // Non-word boundary: flush any pending token.
                if (!token.empty()) {
                    ++st.words;
                    ++st.freq[token];
                    token.clear();
                }
            }
        }
        // Flush last token at end of line (if the line ends with a word character).
        if (!token.empty()) {
            ++st.words;
            ++st.freq[token];
            token.clear();
        }
    }

    // Determine file size in bytes. This is precise and includes newlines.
    std::error_code ec;
    st.bytes = std::filesystem::file_size(conf.input_path, ec);
    if (ec) {
        // Fallback path for special cases (e.g., file_size error, virtual files).
        std::ifstream in2(conf.input_path, std::ios::binary);
        st.bytes = 0;
        char buf[1 << 14]; // 16 KB buffer
        while (in2.read(buf, sizeof(buf)) || in2.gcount() > 0) {
            st.bytes += static_cast<std::uint64_t>(in2.gcount());
        }
    }

    return st;
}

// Return the top-K (word, count) pairs by frequency (desc), breaking ties by word (asc).
static std::vector<std::pair<std::string, std::uint64_t>> top_k(const std::unordered_map<std::string, std::uint64_t>& freq, std::size_t k) {
    std::vector<std::pair<std::string, std::uint64_t>> v(freq.begin(), freq.end());
    // partial_sort is more efficient than full sort when we only need the top K.
    std::partial_sort(v.begin(), v.begin() + std::min(k, v.size()), v.end(), [](auto& a, auto& b) {
        if (a.second != b.second) return a.second > b.second; // higher count first
        return a.first < b.first;                              // then lexicographic
    });
    if (v.size() > k) v.resize(k);
    return v;
}

// Produce an ISO 8601 UTC timestamp string (e.g., "2025-09-22T17:03:00Z").
static std::string iso8601_utc_now() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// Minimal JSON string escaping (quotes, backslashes, and control chars).
static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;        
            default:
                if (c < 0x20) { // Other control characters → \u00XX
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

// Serialize the Stats and top words to a JSON file.
static void write_json(const Config& conf, const Stats& st, const std::vector<std::pair<std::string, std::uint64_t>>& top) {
    std::ofstream out(conf.json_path);
    if (!out) {
        throw std::runtime_error("Cannot write JSON file: " + conf.json_path);
    }

    out << "{\n";
    out << "  \"tool\": \"file-stats\",\n";
    out << "  \"timestamp\": \"" << iso8601_utc_now() << "\",\n";
    out << "  \"input_path\": \"" << json_escape(conf.input_path) << "\",\n";
    out << "  \"lines\": " << st.lines << ",\n";
    out << "  \"words\": " << st.words << ",\n";
    out << "  \"bytes\": " << st.bytes << ",\n";
    out << "  \"case_sensitive\": " << (conf.case_sensitive ? "true" : "false") << ",\n";
    out << "  \"top_words\": [\n";
    for (std::size_t i = 0; i < top.size(); ++i) {
        out << "    { \"word\": \"" << json_escape(top[i].first)
            << "\", \"count\": " << top[i].second << " }";
        if (i + 1 < top.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false); // Faster I/O for large files
    std::setlocale(LC_ALL, "");       // Optional: enable system C-locale (safe under Windows)

    Config conf;
    if (!parse_args(argc, argv, conf)) {
        print_help(argv[0]);
        return 1;                      // Bad usage → show help and exit non-zero
    }

    try {
        Stats st = analyze_file(conf);
        auto top = top_k(st.freq, conf.topN);

        // Human-readable report.
        std::cout << "File:   " << conf.input_path << "\n";
        std::cout << "Lines:  " << st.lines << "\n";
        std::cout << "Words:  " << st.words << "\n";
        std::cout << "Bytes:  " << st.bytes << "\n";
        std::cout << "Top " << top.size() << " words"
                  << (conf.case_sensitive ? " (case-sensitive)" : " (case-insensitive)")
                  << ":\n";

        for (const auto& [w, c] : top) {
            std::cout << "  " << std::setw(8) << c << "  " << w << "\n";
        }

        // Optional JSON export.
        if (!conf.json_path.empty()) {
            write_json(conf, st, top);
            std::cout << "\nJSON written to: " << conf.json_path << "\n";
        }

        return 0; // Success
    } catch (const std::exception& ex) {
        // Any I/O or processing error is reported here.
        std::cerr << "Error: " << ex.what() << "\n";
        return 2;
    }
}
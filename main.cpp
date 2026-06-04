#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// --- (7) Сокеты: POSIX ---
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

// =====================================================================
// (1) ОСНОВНЫЕ КОНСТРУКЦИИ: namespace, типы-алиасы, константы
// =====================================================================
namespace yt {

using i64 = long long;
using Str = std::string;

// (3) МАССИВЫ: std::array фиксированной длины
static const std::array<const char*, 7> DAY_NAMES = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// =====================================================================
// (5) ДИНАМИЧЕСКАЯ СТРУКТУРА ДАННЫХ: собственный односвязный список
//      Используется для накопления записей по каналам перед сортировкой.
//      Демонстрирует ручное управление памятью (new/delete) и шаблоны.
// =====================================================================
template <typename T>
class LinkedList {
    struct Node {
        T value;
        Node* next;
        Node(const T& v, Node* n) : value(v), next(n) {}
    };
    Node* head_ = nullptr;
    size_t size_ = 0;

public:
    LinkedList() = default;
    LinkedList(const LinkedList&) = delete;
    LinkedList& operator=(const LinkedList&) = delete;

    ~LinkedList() { clear(); }

    void push_front(const T& v) {                    // O(1)
        head_ = new Node(v, head_);
        ++size_;
    }

    void clear() {
        // (2) ЦИКЛ while
        while (head_) {
            Node* n = head_->next;
            delete head_;
            head_ = n;
        }
        size_ = 0;
    }

    size_t size() const { return size_; }

    // Преобразование в vector для последующих STL-алгоритмов
    std::vector<T> to_vector() const {
        std::vector<T> out;
        out.reserve(size_);
        for (Node* n = head_; n != nullptr; n = n->next) out.push_back(n->value);
        return out;
    }
};

// =====================================================================
// Структура одной строки датасета
// =====================================================================
struct Row {
    Str video_id;
    Str title;
    Str channelTitle;
    Str description;
    std::time_t publishedAt = 0;
    std::time_t trending_date = 0;
    i64 view_count = 0;
    i64 likes = 0;
    i64 comment_count = 0;
    int publish_hour = 0;
    Str publish_day;
    int days_to_trend = 0;
};

// =====================================================================
// CSV-парсер (RFC 4180-ish, поддержка кавычек и встроенных переводов строк)
// =====================================================================
class CsvReader {
    std::istream& in_;
public:
    explicit CsvReader(std::istream& in) : in_(in) {}

    bool next_record(std::vector<Str>& fields) {
        fields.clear();
        Str field;
        bool in_quotes = false, any = false;
        char c;
        // (2) ЦИКЛ while + ветвления if/else
        while (in_.get(c)) {
            any = true;
            if (in_quotes) {
                if (c == '"') {
                    if (in_.peek() == '"') { in_.get(); field += '"'; }
                    else in_quotes = false;
                } else field += c;
            } else {
                if (c == '"') in_quotes = true;
                else if (c == ',') { fields.push_back(std::move(field)); field.clear(); }
                else if (c == '\r') {}
                else if (c == '\n') { fields.push_back(std::move(field)); return true; }
                else field += c;
            }
        }
        if (any) { fields.push_back(std::move(field)); return true; }
        return false;
    }
};

// =====================================================================
// Вспомогательные функции (свободные)
// =====================================================================
static Str strip_bom(const Str& s) {
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF
        && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        return s.substr(3);
    return s;
}

static Str trim(const Str& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static i64 to_ll(const Str& s) {
    if (s.empty()) return 0;
    try { return std::stoll(s); } catch (...) { return 0; }
}

static bool parse_ts(const Str& s, std::tm& tm_out) {
    if (s.size() < 10) return false;
    std::tm tm{};
    int Y, M, D, h = 0, mi = 0, se = 0;
    char sep = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%d%c%d:%d:%d",
                    &Y, &M, &D, &sep, &h, &mi, &se) >= 3) {
        tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D;
        tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = se;
        tm_out = tm;
        return true;
    }
    return false;
}

static std::time_t to_utc(std::tm tm) { return timegm(&tm); }

static Str fmt_int(i64 v) {
    Str s = std::to_string(v), out;
    int cnt = 0;
    // (2) ЦИКЛ for со счётчиком
    for (int i = (int)s.size() - 1; i >= 0; --i) {
        out += s[i];
        if (++cnt == 3 && i > 0 && s[i - 1] != '-') { out += ','; cnt = 0; }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

static Str json_escape(const Str& s) {
    Str out;
    out.reserve(s.size() + 8);
    // (2) range-based for — STL-стиль итерации
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else out += c;
        }
    }
    return out;
}

static Str now_str(const char* fmt) {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    char buf[64];
    std::strftime(buf, sizeof(buf), fmt, &tm);
    return buf;
}

static Str url_decode(const Str& s) {
    Str out; out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') out += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) {
            char hex[3] = {s[i+1], s[i+2], 0};
            out += (char)std::strtol(hex, nullptr, 16);
            i += 2;
        } else out += s[i];
    }
    return out;
}

// =====================================================================
// Контейнер данных
// =====================================================================
class Dataset {
public:
    std::vector<Row> rows;             // (6) STL: vector
    std::vector<size_t> unique_idx;    // индексы уникальных видео (по video_id)

    // Агрегаты для отчёта
    std::vector<std::pair<Str, i64>> top_channels;
    std::vector<std::pair<int, i64>> hourly;
    std::vector<std::pair<int, i64>> days_to_trend;

    // Метрики
    size_t total_rows = 0;
    size_t unique_videos = 0;
    size_t unique_channels = 0;
    i64 max_views = 0;

    bool load(const Str& path) {
        std::ifstream fin(path);
        if (!fin) { std::cerr << "Cannot open " << path << "\n"; return false; }

        CsvReader reader(fin);
        std::vector<Str> rec;
        if (!reader.next_record(rec)) return false;

        for (auto& c : rec) c = trim(strip_bom(c));
        if (!rec.empty() && rec[0] != "video_id") rec[0] = "video_id";

        // (6) STL: unordered_map для индексации колонок
        std::unordered_map<Str, int> col;
        for (size_t i = 0; i < rec.size(); ++i) col[rec[i]] = (int)i;

        auto idx = [&](const char* n) -> int {
            auto it = col.find(n);
            return it == col.end() ? -1 : it->second;
        };
        const int I_ID = idx("video_id"),    I_TITLE = idx("title");
        const int I_CH = idx("channelTitle"),I_DESC  = idx("description");
        const int I_PUB= idx("publishedAt"), I_TR    = idx("trending_date");
        const int I_VW = idx("view_count"),  I_LK    = idx("likes");
        const int I_CM = idx("comment_count");

        rows.reserve(250000);
        // (2) ЦИКЛ while — чтение всех строк
        while (reader.next_record(rec)) {
            if (rec.empty()) continue;
            auto g = [&](int i) -> const Str& {
                static const Str empty;
                return (i >= 0 && i < (int)rec.size()) ? rec[i] : empty;
            };
            Row r;
            r.video_id     = g(I_ID);
            r.title        = g(I_TITLE);
            r.channelTitle = g(I_CH);
            r.description  = g(I_DESC).empty() ? "No description" : g(I_DESC);
            r.view_count    = to_ll(g(I_VW));
            r.likes         = to_ll(g(I_LK));
            r.comment_count = to_ll(g(I_CM));

            std::tm tp{}, tt{};
            if (!parse_ts(g(I_PUB), tp) || !parse_ts(g(I_TR), tt)) continue;
            r.publishedAt = to_utc(tp);
            r.trending_date = to_utc(tt);
            std::tm pn = *std::gmtime(&r.publishedAt);
            r.publish_hour = pn.tm_hour;
            r.publish_day  = DAY_NAMES[pn.tm_wday];
            long d = (long)((r.trending_date - r.publishedAt) / 86400);
            r.days_to_trend = d < 0 ? 0 : (int)d;
            rows.push_back(std::move(r));
        }

        compute_uniques_and_metrics();
        return true;
    }

private:
    void compute_uniques_and_metrics() {
        // drop_duplicates(subset='video_id', keep='last')
        std::unordered_map<Str, size_t> last_pos;
        for (size_t i = 0; i < rows.size(); ++i) last_pos[rows[i].video_id] = i;
        unique_idx.reserve(last_pos.size());
        for (auto& kv : last_pos) unique_idx.push_back(kv.second);
        std::sort(unique_idx.begin(), unique_idx.end());

        total_rows = rows.size();
        unique_videos = unique_idx.size();

        // (6) STL: unordered_set
        std::unordered_set<Str> chans;
        for (size_t i : unique_idx) {
            chans.insert(rows[i].channelTitle);
            if (rows[i].view_count > max_views) max_views = rows[i].view_count;
        }
        unique_channels = chans.size();

        // Часовое распределение
        std::map<int, i64> h;
        for (size_t i : unique_idx) h[rows[i].publish_hour]++;
        for (auto& kv : h) hourly.push_back(kv);

        // Дни до трендов: топ-10 самых частых
        std::unordered_map<int, i64> dm;
        for (size_t i : unique_idx) dm[rows[i].days_to_trend]++;
        std::vector<std::pair<int, i64>> dv(dm.begin(), dm.end());
        std::sort(dv.begin(), dv.end(),
                  [](auto& a, auto& b){ return a.second > b.second; });
        if (dv.size() > 10) dv.resize(10);
        std::sort(dv.begin(), dv.end(),
                  [](auto& a, auto& b){ return a.first < b.first; });
        days_to_trend = std::move(dv);
    }

public:
    // Топ каналов вычисляется по запросу (зависит от N)
    void recompute_top_channels(int n) {
        // (5) Применяем собственный связный список перед свёрткой в vector
        std::unordered_map<Str, i64> cnt;
        for (auto& r : rows) cnt[r.channelTitle]++;

        LinkedList<std::pair<Str, i64>> chain;
        for (auto& kv : cnt) chain.push_front(kv);

        auto vec = chain.to_vector();
        std::sort(vec.begin(), vec.end(),
                  [](auto& a, auto& b){ return a.second > b.second; });
        if ((int)vec.size() > n) vec.resize(n);
        top_channels = std::move(vec);
    }

    // Случайная выборка индексов для scatter-графика
    std::vector<size_t> sample(int k) const {
        std::vector<size_t> s = unique_idx;
        std::mt19937 rng(42);
        std::shuffle(s.begin(), s.end(), rng);
        if ((int)s.size() > k) s.resize(k);
        return s;
    }
};

// =====================================================================
// (4) ООП: базовый абстрактный класс Analyzer + наследники.
//      Каждый наследник умеет отрендерить свой график (Plotly-trace JSON).
// =====================================================================
class Analyzer {
public:
    virtual ~Analyzer() = default;
    virtual Str id() const = 0;            // HTML id контейнера
    virtual Str title() const = 0;         // Заголовок секции
    virtual Str height() const { return "450px"; }
    virtual Str plotly_js(const Dataset& ds) const = 0;
};

// JSON-помощники (свободные — пригодятся всем наследникам)
namespace jhelp {
    static Str arr_i64(const std::vector<i64>& v) {
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < v.size(); ++i) { if (i) o << ","; o << v[i]; }
        o << "]"; return o.str();
    }
    static Str arr_int(const std::vector<int>& v) {
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < v.size(); ++i) { if (i) o << ","; o << v[i]; }
        o << "]"; return o.str();
    }
    static Str arr_str(const std::vector<Str>& v) {
        std::ostringstream o; o << "[";
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) o << ",";
            o << "\"" << json_escape(v[i]) << "\"";
        }
        o << "]"; return o.str();
    }
    static Str layout_base() {
        return "{paper_bgcolor:'#1e1e2e',plot_bgcolor:'#1e1e2e',font:{color:'#fafafa'}}";
    }
}

class TopChannelsAnalyzer : public Analyzer {
    int top_n_;
public:
    explicit TopChannelsAnalyzer(int n) : top_n_(n) {}
    Str id() const override { return "fig_channels"; }
    Str title() const override { return "🏆 Рейтинги каналов"; }
    Str height() const override { return "600px"; }
    Str plotly_js(const Dataset& ds) const override {
        std::vector<Str> names; std::vector<i64> vals;
        // Реверс для горизонтальной полосы (по возрастанию)
        for (auto it = ds.top_channels.rbegin(); it != ds.top_channels.rend(); ++it) {
            names.push_back(it->first);
            vals.push_back(it->second);
        }
        std::ostringstream o;
        o << "Plotly.newPlot('" << id() << "',[{"
          << "type:'bar',orientation:'h',"
          << "x:" << jhelp::arr_i64(vals) << ","
          << "y:" << jhelp::arr_str(names) << ","
          << "marker:{color:" << jhelp::arr_i64(vals) << ",colorscale:'Viridis'}"
          << "}],Object.assign({}," << jhelp::layout_base() << ",{"
          << "title:'Топ-" << top_n_ << " каналов по времени удержания в трендах',"
          << "xaxis:{title:'Дней в трендах'},yaxis:{automargin:true}}));";
        return o.str();
    }
};

class ScatterAnalyzer : public Analyzer {
    int sample_size_;
public:
    explicit ScatterAnalyzer(int n) : sample_size_(n) {}
    Str id() const override { return "fig_scatter"; }
    Str title() const override { return "🔍 Просмотры vs Лайки"; }
    Str height() const override { return "600px"; }
    Str plotly_js(const Dataset& ds) const override {
        auto sample = ds.sample(sample_size_);
        std::vector<i64> sx, sy, sz;
        std::vector<Str> st, sc;
        i64 max_sz = 1;
        // (2) range-based for
        for (size_t i : sample) {
            sx.push_back(ds.rows[i].view_count);
            sy.push_back(ds.rows[i].likes);
            sz.push_back(ds.rows[i].comment_count);
            st.push_back(ds.rows[i].title);
            sc.push_back(ds.rows[i].channelTitle);
            if (ds.rows[i].comment_count > max_sz) max_sz = ds.rows[i].comment_count;
        }
        double sizeref = 2.0 * (double)max_sz / (40.0 * 40.0);
        std::ostringstream o;
        o << "Plotly.newPlot('" << id() << "',[{"
          << "type:'scatter',mode:'markers',"
          << "x:" << jhelp::arr_i64(sx) << ","
          << "y:" << jhelp::arr_i64(sy) << ","
          << "text:" << jhelp::arr_str(st) << ","
          << "customdata:" << jhelp::arr_str(sc) << ","
          << "hovertemplate:'<b>%{text}</b><br>Канал: %{customdata}"
             "<br>Просмотры: %{x:,}<br>Лайки: %{y:,}<extra></extra>',"
          << "marker:{size:" << jhelp::arr_i64(sz)
          << ",sizemode:'area',sizeref:" << sizeref << ",sizemin:4,"
          << "color:" << jhelp::arr_i64(sx) << ",colorscale:'Reds',showscale:false}"
          << "}],Object.assign({}," << jhelp::layout_base() << ",{"
          << "title:'Связь Просмотров и Лайков (размер = комментарии)',"
          << "xaxis:{title:'Просмотры'},yaxis:{title:'Лайки'}}));";
        return o.str();
    }
};

class HourlyAnalyzer : public Analyzer {
public:
    Str id() const override { return "fig_hours"; }
    Str title() const override { return "🕐 Активность по часам публикации"; }
    Str plotly_js(const Dataset& ds) const override {
        std::vector<int> x; std::vector<i64> y;
        for (auto& kv : ds.hourly) { x.push_back(kv.first); y.push_back(kv.second); }
        std::ostringstream o;
        o << "Plotly.newPlot('" << id() << "',[{"
          << "type:'scatter',mode:'lines+markers',"
          << "x:" << jhelp::arr_int(x) << ","
          << "y:" << jhelp::arr_i64(y)
          << "}],Object.assign({}," << jhelp::layout_base() << ",{"
          << "title:'Распределение публикаций по часам суток (UTC)',"
          << "xaxis:{title:'Час публикации'},yaxis:{title:'Количество видео'}}));";
        return o.str();
    }
};

class DaysToTrendAnalyzer : public Analyzer {
public:
    Str id() const override { return "fig_days"; }
    Str title() const override { return "⚡ Скорость попадания в тренды"; }
    Str plotly_js(const Dataset& ds) const override {
        std::vector<int> x; std::vector<i64> y;
        for (auto& kv : ds.days_to_trend) { x.push_back(kv.first); y.push_back(kv.second); }
        std::ostringstream o;
        o << "Plotly.newPlot('" << id() << "',[{"
          << "type:'bar',"
          << "x:" << jhelp::arr_int(x) << ","
          << "y:" << jhelp::arr_i64(y) << ","
          << "marker:{color:" << jhelp::arr_i64(y) << ",colorscale:'Viridis'}"
          << "}],Object.assign({}," << jhelp::layout_base() << ",{"
          << "title:'Скорость попадания видео в топ после релиза',"
          << "xaxis:{title:'Дней до трендов'},yaxis:{title:'Количество видео'}}));";
        return o.str();
    }
};

// =====================================================================
// Сборщик HTML-страницы
// =====================================================================
class ReportBuilder {
    const Dataset& ds_;
    int top_n_, sample_size_;
    Str search_;
public:
    ReportBuilder(const Dataset& d, int t, int s, Str q)
        : ds_(d), top_n_(t), sample_size_(s), search_(std::move(q)) {}

    Str build() const {
        // (6) STL: vector<unique_ptr> — полиморфные анализаторы
        std::vector<std::unique_ptr<Analyzer>> analyzers;
        analyzers.emplace_back(std::make_unique<TopChannelsAnalyzer>(top_n_));
        analyzers.emplace_back(std::make_unique<ScatterAnalyzer>(sample_size_));
        analyzers.emplace_back(std::make_unique<HourlyAnalyzer>());
        analyzers.emplace_back(std::make_unique<DaysToTrendAnalyzer>());

        std::ostringstream h;
        h << "<!DOCTYPE html><html lang=\"ru\"><head>"
          << "<meta charset=\"UTF-8\">"
          << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
          << "<title>YouTube Trending Analytics</title>"
          << "<style>"
          << "body{font-family:sans-serif;background:#0e1117;color:#fafafa;margin:0;padding:20px}"
          << "h1{color:#ff4b4b}h2{color:#fafafa;border-bottom:1px solid #333;"
             "padding-bottom:8px;margin-top:40px}"
          << ".subtitle{color:#aaa;margin-bottom:30px}"
          << ".metrics{display:flex;gap:20px;flex-wrap:wrap;margin:20px 0}"
          << ".metric{background:#1e1e2e;border-radius:8px;padding:20px 30px;"
             "flex:1;min-width:150px}"
          << ".metric-label{font-size:13px;color:#aaa;margin-bottom:6px}"
          << ".metric-value{font-size:28px;font-weight:bold;color:#ff4b4b}"
          << ".chart{background:#1e1e2e;border-radius:8px;padding:10px;margin:20px 0}"
          << ".filter-badge{display:inline-block;background:#333;border-radius:4px;"
             "padding:3px 10px;font-size:12px;color:#aaa;margin-bottom:10px}"
          << "form.controls{background:#1e1e2e;padding:16px;border-radius:8px;"
             "margin-bottom:24px;display:flex;gap:16px;flex-wrap:wrap;align-items:end}"
          << "form.controls label{display:flex;flex-direction:column;font-size:12px;color:#aaa}"
          << "form.controls input{background:#0e1117;color:#fafafa;border:1px solid #444;"
             "border-radius:4px;padding:6px 10px;margin-top:4px}"
          << "form.controls button{background:#ff4b4b;color:#fff;border:0;border-radius:4px;"
             "padding:8px 16px;cursor:pointer;font-weight:bold}"
          << "table{width:100%;border-collapse:collapse;margin-top:12px}"
          << "th,td{padding:8px 12px;border-bottom:1px solid #333;text-align:left;font-size:13px}"
          << "th{color:#ff4b4b}"
          << ".footer{text-align:center;color:#555;margin-top:60px;font-size:12px;"
             "border-top:1px solid #222;padding-top:20px}"
          << "</style>"
          << "<script src=\"https://cdn.plot.l" "y/plotly-2.35.2.min.js\"></script>"
          << "</head><body>"
          << "<h1>📊 Интерактивный анализ трендов YouTube (RU)</h1>"
          << "<p class=\"subtitle\">C++ HTTP-сервер на сокетах &nbsp;·&nbsp; "
          << now_str("%d.%m.%Y %H:%M") << "</p>"
          // --- Форма управления (GET-параметры) ---
          << "<form class=\"controls\" method=\"get\" action=\"/\">"
          << "<label>Топ каналов<input type=\"number\" name=\"top_n\" min=\"5\" max=\"100\" value=\""
          << top_n_ << "\"></label>"
          << "<label>Размер выборки<input type=\"number\" name=\"sample_size\" min=\"500\" "
             "max=\"100000\" value=\"" << sample_size_ << "\"></label>"
          << "<label>Поиск<input type=\"text\" name=\"search\" value=\""
          << json_escape(search_) << "\" placeholder=\"название/канал\"></label>"
          << "<button type=\"submit\">Применить</button>"
          << "</form>"
          // --- Метрики ---
          << "<h2>📈 Общие показатели датасета</h2>"
          << "<div class=\"metrics\">"
          << "<div class=\"metric\"><div class=\"metric-label\">Всего записей</div>"
             "<div class=\"metric-value\">" << fmt_int(ds_.total_rows) << "</div></div>"
          << "<div class=\"metric\"><div class=\"metric-label\">Уникальных видео</div>"
             "<div class=\"metric-value\">" << fmt_int(ds_.unique_videos) << "</div></div>"
          << "<div class=\"metric\"><div class=\"metric-label\">Уникальных каналов</div>"
             "<div class=\"metric-value\">" << fmt_int(ds_.unique_channels) << "</div></div>"
          << "<div class=\"metric\"><div class=\"metric-label\">Максимум просмотров</div>"
             "<div class=\"metric-value\">" << fmt_int(ds_.max_views) << "</div></div>"
          << "</div>";

        // --- Секции анализаторов (полиморфизм) ---
        for (auto& a : analyzers) {
            h << "<h2>" << a->title() << "</h2>"
              << "<div class=\"chart\"><div id=\"" << a->id()
              << "\" style=\"height:" << a->height() << ";\"></div></div>";
        }

        // --- Таблица поиска ---
        if (!search_.empty()) {
            h << "<h2>📋 Результаты поиска: \""
              << json_escape(search_) << "\"</h2>";
            h << "<table><thead><tr><th>Название</th><th>Канал</th>"
                 "<th>Просмотры</th><th>Лайки</th><th>Коммент.</th></tr></thead><tbody>";
            Str q = search_;
            std::transform(q.begin(), q.end(), q.begin(), ::tolower);
            int shown = 0;
            for (size_t i : ds_.unique_idx) {
                Str t = ds_.rows[i].title, c = ds_.rows[i].channelTitle;
                Str tl = t, cl = c;
                std::transform(tl.begin(), tl.end(), tl.begin(), ::tolower);
                std::transform(cl.begin(), cl.end(), cl.begin(), ::tolower);
                if (tl.find(q) != Str::npos || cl.find(q) != Str::npos) {
                    h << "<tr><td>" << json_escape(t) << "</td>"
                      << "<td>" << json_escape(c) << "</td>"
                      << "<td>" << fmt_int(ds_.rows[i].view_count) << "</td>"
                      << "<td>" << fmt_int(ds_.rows[i].likes) << "</td>"
                      << "<td>" << fmt_int(ds_.rows[i].comment_count) << "</td></tr>";
                    if (++shown >= 100) break;
                }
            }
            h << "</tbody></table>";
        }

        h << "<div class=\"footer\">YouTube Trending Analytics · C++ web server</div>"
          << "<script>";
        for (auto& a : analyzers) h << a->plotly_js(ds_);
        h << "</script></body></html>";
        return h.str();
    }
};

// =====================================================================
// (7) HTTP-сервер на BSD-сокетах
// =====================================================================
class HttpServer {
    Dataset& ds_;
    int port_;
    int sock_ = -1;

    static std::unordered_map<Str, Str> parse_query(const Str& qs) {
        std::unordered_map<Str, Str> out;
        size_t i = 0;
        while (i < qs.size()) {
            size_t amp = qs.find('&', i);
            Str pair = qs.substr(i, amp == Str::npos ? Str::npos : amp - i);
            size_t eq = pair.find('=');
            if (eq != Str::npos)
                out[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
            if (amp == Str::npos) break;
            i = amp + 1;
        }
        return out;
    }

    void handle_client(int client) {
        // Читаем запрос (только заголовки, для GET достаточно)
        std::array<char, 8192> buf{};   // (3) std::array
        ssize_t n = recv(client, buf.data(), buf.size() - 1, 0);
        if (n <= 0) { close(client); return; }
        buf[n] = '\0';

        // Разбор первой строки: METHOD PATH HTTP/x.x
        Str req(buf.data());
        size_t sp1 = req.find(' ');
        size_t sp2 = req.find(' ', sp1 + 1);
        Str method = req.substr(0, sp1);
        Str path   = req.substr(sp1 + 1, sp2 - sp1 - 1);

        Str body, content_type = "text/html; charset=utf-8";
        int status = 200; Str status_text = "OK";

        if (method != "GET") {
            status = 405; status_text = "Method Not Allowed";
            body = "<h1>405 Method Not Allowed</h1>";
        } else if (path == "/favicon.ico") {
            status = 204; status_text = "No Content";
            content_type = "image/x-icon";
        } else if (path.rfind("/", 0) == 0) {
            // Параметры из query-string
            size_t qpos = path.find('?');
            Str qs = (qpos == Str::npos) ? "" : path.substr(qpos + 1);
            auto params = parse_query(qs);

            int top_n = 10, sample_size = 10000;
            Str search;
            if (params.count("top_n"))       top_n       = std::max(1, std::stoi(params["top_n"]));
            if (params.count("sample_size")) sample_size = std::max(100, std::stoi(params["sample_size"]));
            if (params.count("search"))      search      = params["search"];

            ds_.recompute_top_channels(top_n);
            ReportBuilder rb(ds_, top_n, sample_size, search);
            body = rb.build();
        } else {
            status = 404; status_text = "Not Found";
            body = "<h1>404 Not Found</h1>";
        }

        std::ostringstream resp;
        resp << "HTTP/1.1 " << status << " " << status_text << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << body;
        Str s = resp.str();
        send(client, s.data(), s.size(), 0);
        close(client);
    }

public:
    HttpServer(Dataset& d, int port) : ds_(d), port_(port) {}

    bool start() {
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ < 0) { std::cerr << "socket() failed\n"; return false; }
        int yes = 1;
        setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port_);
        if (bind(sock_, (sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "bind() failed (порт занят?)\n"; return false;
        }
        if (listen(sock_, 16) < 0) { std::cerr << "listen() failed\n"; return false; }

        std::cout << "Сервер запущен: http://localhost:" << port_ << "\n";
        std::cout << "Нажмите Ctrl+C для остановки.\n";

        // (2) Бесконечный цикл приёма соединений
        while (true) {
            sockaddr_in cli{}; socklen_t cl = sizeof(cli);
            int c = accept(sock_, (sockaddr*)&cli, &cl);
            if (c < 0) continue;
            // По потоку на клиента (учебный пример, не для прод-нагрузки)
            std::thread(&HttpServer::handle_client, this, c).detach();
        }
        return true;
    }

    ~HttpServer() { if (sock_ >= 0) close(sock_); }
};

} // namespace yt

// =====================================================================
// main: разбор аргументов, загрузка датасета, запуск сервера
// =====================================================================
int main(int argc, char** argv) {
    std::string csv = "RU_youtube_trending_data.csv";
    int port = 8080;

    // (2) ЦИКЛ for со счётчиком — разбор argv
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        if ((k == "--csv" || k == "--port") && i + 1 >= argc) {
            std::cerr << "Missing value for " << k << "\n"; return 2;
        }
        if (k == "--csv")       csv  = argv[++i];
        else if (k == "--port") port = std::stoi(argv[++i]);
        else if (k == "--help" || k == "-h") {
            std::cout << "Usage: yt_server [--csv FILE] [--port N]\n";
            return 0;
        }
    }

    std::cout << "Загрузка датасета: " << csv << " ...\n";
    yt::Dataset ds;
    if (!ds.load(csv)) return 1;
    std::cout << "Загружено строк: " << ds.total_rows
              << ", уникальных видео: " << ds.unique_videos << "\n";

    yt::HttpServer server(ds, port);
    server.start();
    return 0;
}

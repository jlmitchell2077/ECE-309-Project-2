#ifdef NDEBUG
#undef NDEBUG
#endif
#include "core/conversation.h"
#include "core/sentinel_scanner.h"
#include "harness/harness.h"
#include "model/replay_client.h"
#include "model/scripted_client.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

struct P2TestAccess {
    static std::size_t capacity(const Conversation& c) { return c.capacity_; }
    static std::size_t pending(const SentinelScanner& s) { return s.pending_.size(); }
};
namespace {
const std::string sentinel = "<|end_conversation|>";
struct Input : InputSource {
    explicit Input(std::string text) : stream(std::move(text)) {}
    std::istringstream stream;
    bool eof = false;
    std::string read_line() override {
        std::string line;
        eof = !static_cast<bool>(std::getline(stream, line));
        return line;
    }
    bool is_eof() const override { return eof; }
};
struct Output : OutputSink {
    std::string text;
    void write(std::string_view s) override { text += s; }
};
struct TempFile {
    std::filesystem::path path;
    explicit TempFile(const std::string& text) {
        static unsigned counter = 0;
        path = std::filesystem::temp_directory_path() /
            ("p2-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())
             + "-" + std::to_string(counter++) + ".txt");
        std::ofstream out(path);
        out << text;
        out.close();
        assert(out.good());
    }
    ~TempFile() { std::error_code ec; std::filesystem::remove(path, ec); }
};
void same(const Conversation& a, const Conversation& b) {
    assert(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        assert(a.at(i).role() == b.at(i).role());
        assert(a.at(i).content() == b.at(i).content());
    }
}
void empty_bounds() {
    Conversation c;
    assert(c.size() == 0 && c.begin() == nullptr && c.end() == c.begin());
    assert(P2TestAccess::capacity(c) == 0);
    bool threw = false;
    try { (void)c.at(0); } catch (const std::out_of_range&) { threw = true; }
    assert(threw);
    c.append({Role::User, "x"});
    threw = false;
    try { (void)c.at(c.size()); } catch (const std::out_of_range&) { threw = true; }
    assert(threw);
}
void growth_and_order() {
    Conversation c;
    c.append({Role::System, "pinned"});
    std::size_t capacity = 1;
    for (std::size_t i = 1; i < 4097; ++i) {
        const Message* previous = c.begin();
        const bool grow = c.size() == capacity;
        c.append({i % 2 ? Role::User : Role::Assistant, std::to_string(i)});
        if (grow) capacity *= 2;
        assert(P2TestAccess::capacity(c) == capacity);
        assert((c.begin() != previous) == grow);
        assert(c.size() == i + 1);
        assert(c.at(0).role() == Role::System && c.at(0).content() == "pinned");
        for (std::size_t j = 1; j <= i; ++j) assert(c.at(j).content() == std::to_string(j));
    }
    std::size_t count = 0;
    for (const Message& m : c) { assert(&m == &c.at(count)); ++count; }
    assert(count == c.size());
}
void copy_constructor() {
    Conversation original;
    original.append({Role::User, std::string(200, 'a')});
    Conversation copy(original);
    same(original, copy);
    assert(copy.begin() != original.begin());
    assert(copy.at(0).content().data() != original.at(0).content().data());
    original = Conversation{};
    assert(copy.at(0).content() == std::string(200, 'a'));
    Conversation empty;
    Conversation empty_copy(empty);
    same(empty, empty_copy);
}
void move_constructor() {
    static_assert(std::is_nothrow_move_constructible<Conversation>::value);
    Conversation a;
    a.append({Role::User, "owned"});
    auto* pointer = a.begin();
    Conversation b(std::move(a));
    assert(b.begin() == pointer && b.at(0).content() == "owned");
    assert(a.size() == 0 && a.begin() == nullptr && a.end() == nullptr);
    assert(P2TestAccess::capacity(a) == 0);
    a.append({Role::User, "reused"});
    assert(a.at(0).content() == "reused");
}
void clean_text() {
    SentinelScanner s(sentinel);
    auto a = s.feed("Hello ");
    auto b = s.feed("world!");
    auto c = s.flush();
    assert(a.safe_text + b.safe_text + c.safe_text == "Hello world!");
    assert(!a.sentinel_found && !b.sentinel_found && !c.sentinel_found);
    assert(s.flush().safe_text.empty());
}
void every_boundary() {
    const std::string text = "Goodbye." + sentinel + "discarded";
    for (std::size_t split = 0; split <= text.size(); ++split) {
        SentinelScanner s(sentinel);
        auto a = s.feed(std::string_view(text).substr(0, split));
        auto b = s.feed(std::string_view(text).substr(split));
        assert(a.sentinel_found || b.sentinel_found);
        assert(a.safe_text + b.safe_text == "Goodbye.");
        assert(s.flush().safe_text.empty());
        assert(s.feed("ignored").safe_text.empty());
    }
}
void false_alarms_and_flush() {
    const std::string text = "<|end_world|><<|end_conversation|x<|end_";
    SentinelScanner s(sentinel);
    std::string safe;
    for (char ch : text) {
        auto r = s.feed(std::string_view(&ch, 1));
        assert(!r.sentinel_found);
        safe += r.safe_text;
    }
    safe += s.flush().safe_text;
    assert(safe == text);
}
void bounded_adversarial() {
    SentinelScanner s(sentinel);
    const std::string pattern = "<|end_<|end_conversation|x";
    constexpr std::size_t bytes = 4 * 1024 * 1024;
    std::size_t emitted = 0;
    for (std::size_t i = 0; i < bytes; ++i) {
        char ch = pattern[i % pattern.size()];
        auto r = s.feed(std::string_view(&ch, 1));
        assert(!r.sentinel_found);
        assert(P2TestAccess::pending(s) <= sentinel.size() - 1);
        for (char output_char : r.safe_text) {
            assert(output_char == pattern[emitted % pattern.size()]);
            emitted++;
        }
    }
    SentinelScanner::Out remaining = s.flush();
    for (char output_char : remaining.safe_text) {
        assert(output_char == pattern[emitted % pattern.size()]);
        emitted++;
    }
    assert(emitted == bytes);
}
std::string script() {
    return "role: system\nBe concise.\n---\nchunk: 1\nrole: assistant\nHello.\n---\n"
           "chunk: 1\nrole: assistant\nGoodbye." + sentinel + "discard\n";
}
void turn_limit() {
    TempFile file(script());
    Harness h(std::make_unique<ScriptedModelClient>(file.path.string()), {1, "pinned"});
    Input in("hello\nbye\n"); Output out;
    assert(h.run(in, out).kind == StopReason::Kind::TurnLimit);
    assert(h.conversation().size() == 3);
    assert(h.conversation().at(0).role() == Role::System);
    assert(h.conversation().at(0).content() == "pinned");
    assert(out.text == "you> assistant> Hello.\n");
}
void sentinel_halt() {
    TempFile file(script());
    Harness h(std::make_unique<ScriptedModelClient>(file.path.string()), {20, ""});
    Input in("hello\nbye\nunused\n"); Output out;
    auto reason = h.run(in, out);
    assert(reason.kind == StopReason::Kind::Sentinel);
    assert(reason.detail == "stop sentinel after 2 turns");
    assert(h.conversation().size() == 4);
    assert(h.conversation().at(3).content() == "Goodbye." + sentinel);
    assert(out.text == "you> assistant> Hello.\nyou> assistant> Goodbye.\n");
}
void eof_and_blank_input() {
    TempFile file(script());
    Harness h(std::make_unique<ScriptedModelClient>(file.path.string()), {20, ""});
    Input in("\nhello\n"); Output out;
    assert(h.run(in, out).kind == StopReason::Kind::UserExit);
    assert(h.conversation().size() == 2);
    assert(h.conversation().at(0).content() == "hello");
}
void transcript_round_trip() {
    TempFile file(script());
    auto model = std::make_unique<ScriptedModelClient>(file.path.string());
    HarnessConfig cfg{20, model->system_message()};
    Harness original(std::move(model), cfg);
    Input in("hello\nbye\n"); Output out;
    auto reason = original.run(in, out);
    std::ostringstream saved;
    bool first = true;
    for (const Message& m : original.conversation()) {
        if (!first) saved << "---\n";
        first = false;
        const char* role = m.role() == Role::System ? "system" :
                           m.role() == Role::User ? "user" : "assistant";
        saved << "role: " << role << '\n' << m.content() << '\n';
    }
    TempFile transcript(saved.str());
    auto replay = std::make_unique<ReplayModelClient>(transcript.path.string());
    HarnessConfig replay_cfg{20, replay->system_message()};
    Harness replayed(std::move(replay), replay_cfg);
    Input again("hello\nbye\n"); Output replay_out;
    auto replay_reason = replayed.run(again, replay_out);
    assert(reason.kind == replay_reason.kind && reason.detail == replay_reason.detail);
    assert(out.text == replay_out.text);
    same(original.conversation(), replayed.conversation());
}
} // namespace
int main() {
    empty_bounds();
    growth_and_order();
    copy_constructor();
    move_constructor();
    clean_text();
    every_boundary();
    false_alarms_and_flush();
    bounded_adversarial();
    turn_limit();
    sentinel_halt();
    eof_and_blank_input();
    transcript_round_trip();
    std::cout << "12 tests passed\n";
}

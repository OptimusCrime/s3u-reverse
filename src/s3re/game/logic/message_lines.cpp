// The on-screen message lines and the chat input of CLogicThread
// (Volker\LogicDraw.cpp; fields in logic_thread_layout.h, re/logic_thread.md
// "Message lines and chat").
//
// CLogicThread::ShowMessage (0x0051e440): (std::wstring text by value,
//   kind) thiscall, ret 0x14; 9 callers. PushUiFeedback(0x3d) first; the
//   current line (+0x414 + 0x1c * message_index) gets the text (an inlined
//   assign(text, 0, npos): a full copy shares the buffer when its count
//   byte is below 0xfe, otherwise _Grow(count, true) and a copy; the
//   self-assignment branch 0x51e499.. can't run, the text is a stack
//   argument), the kind, a lifetime of 800 ticks (1,000,000 for kinds >=
//   250) and the dirty flag. message_index moves back by one (signed,
//   (count + index - 1) % count; a count of 0 faults as in the original);
//   for kinds below 20 the new current line's lifetime becomes 0. Last the
//   argument is destroyed (inlined: a shared buffer loses a reference,
//   otherwise it is freed). EAX not used by the callers (one reads AL of
//   its own later).
// CLogicThread::ChatInputKey (0x0051e160): (key) thiscall, ret 4, AL. Enter
//   (13): returns 0 and changes nothing. Backspace (8) with more text than
//   the prompt (+0x400 > +0x40c): the chat text (+0x3f8) becomes its first
//   length - 1 characters (a local std::wstring whose allocator byte is the
//   key's low byte gets assign(chat, 0, length - 1) 0x428400; then the
//   inlined chat.assign(local, 0, npos) as in ShowMessage, the local
//   destroyed). Every key but Enter sets the dirty flag +0x410 and returns
//   1. Upper bytes of EAX are the caller's / a callee's in the original;
//   ours are 0.
// The inlined assign(x, 0, npos) of both is std::wstring::assign
// 0x428400's x != this path; ours calls that function (verified).

#include "game/globals.h"
#include "game/logic/logic_thread_layout.h"
#include "game/tables.h"
#include "hooks.h"
#include "msvc6/game_heap.h"
#include "msvc6/wstring.h"
#include "test/callee_stub.h"
#include "test/difftest.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace s3re::game {

namespace {

using msvc6::WString;

constexpr std::uintptr_t kPushUiFeedback = 0x0053f560;  // CLogicThread::PushUiFeedback(value)
constexpr std::uintptr_t kAssignSubstr = 0x00428400;    // std::wstring::assign(x, pos, n)
constexpr std::uint32_t kFeedbackMessage = 0x3d;        // the UI feedback ShowMessage pushes
constexpr std::int32_t kLifetime = 800;                 // ticks a message stays
constexpr std::int32_t kLongLifetime = 1000000;         // kinds >= 250 (tutorial and system texts)
constexpr std::int32_t kLongLivedKinds = 250;
constexpr std::int32_t kHiddenKinds = 20;               // kinds below this don't keep the next line
constexpr std::uint32_t kKeyBackspace = 8;
constexpr std::uint32_t kKeyEnter = 13;
constexpr std::uint8_t kFrozen = 0xff;

#pragma pack(push, 1)
// The chat input fields of CLogicThread (+0x3f8..+0x410).
struct ChatInput {
    WString text;                // +0x3f8
    std::uint8_t unk408[4];      // +0x408 (the active flag is the byte at +0x408)
    std::uint32_t prompt_length; // +0x40c characters of the prompt in front of the input
    std::uint8_t dirty;          // +0x410 1 = redraw the input line
};
#pragma pack(pop)
static_assert(sizeof(ChatInput) == 0x19);

ChatInput* ChatOf(CLogicThread* thread) {
    return reinterpret_cast<ChatInput*>(reinterpret_cast<std::uint8_t*>(thread) + 0x3f8);  // raw-ok: chat fields
}

WString* TextOf(MessageLine& line) {
    return reinterpret_cast<WString*>(line.text);
}

void Assign(WString* self, const WString* x, std::uint32_t pos, std::uint32_t n) {
    reinterpret_cast<msvc6::WStringAssignSubstrFn>(kAssignSubstr)(self, nullptr, x, pos, n);
}

// ~wstring as these functions inline it: one reference less of a shared
// buffer, else operator delete(ptr - 2).
void DestroyText(const WString& s) {
    if (s.ptr == nullptr) {
        return;
    }
    std::uint8_t& count = reinterpret_cast<std::uint8_t*>(s.ptr)[-1];
    if (count != 0 && count != kFrozen) {
        count = static_cast<std::uint8_t>(count - 1);
    } else {
        msvc6::GameOperatorDelete(reinterpret_cast<std::uint8_t*>(s.ptr) - 2);
    }
}

}  // namespace

using ShowMessageFn = void(__fastcall*)(CLogicThread* self, void* edx, WString text, std::int32_t kind);
using ChatInputKeyFn = std::uint32_t(__fastcall*)(CLogicThread* self, void* edx, std::uint32_t key);

// Purpose: Shows a message on the screen (chat, notifications, tutorial
// texts) in the next line of the message area.
void __fastcall CLogicThread_ShowMessage(CLogicThread* self, void*, WString text, std::int32_t kind) {
    using PushFn = std::uint32_t(__fastcall*)(CLogicThread*, void*, std::uint32_t);
    reinterpret_cast<PushFn>(kPushUiFeedback)(self, nullptr, kFeedbackMessage);
    MessageLine& line = self->messages[self->message_index];
    Assign(TextOf(line), &text, 0, g_wstringNpos);
    MessageLine& current = self->messages[self->message_index];
    current.kind = kind;
    current.lifetime = kLifetime;
    current.dirty = 1;
    if (kind >= kLongLivedKinds) {
        current.lifetime = kLongLifetime;
    }
    const std::int32_t count = self->message_count;
    self->message_index = (count + self->message_index - 1) % count;
    if (kind < kHiddenKinds) {
        self->messages[self->message_index].lifetime = 0;
    }
    DestroyText(text);
}

// Purpose: Handles a control key typed into the chat input: Backspace
// removes the last character (not the prompt), Enter is left to the
// caller (it sends the message).
std::uint32_t __fastcall CLogicThread_ChatInputKey(CLogicThread* self, void*, std::uint32_t key) {
    if (key == kKeyEnter) {
        return 0;
    }
    ChatInput* chat = ChatOf(self);
    if (key == kKeyBackspace && chat->text.len > chat->prompt_length) {
        WString shorter{};
        shorter.allocator = static_cast<std::uint8_t>(key);  // (the key argument's byte, as in the original)
        Assign(&shorter, &chat->text, 0, chat->text.len - 1);
        Assign(&chat->text, &shorter, 0, g_wstringNpos);
        DestroyText(shorter);
    }
    chat->dirty = 1;
    return 1;
}

S3RE_HOOK(0x0051e440, CLogicThread_ShowMessage, ShowMessageFn, Original_CLogicThread_ShowMessage,
          CLogicThread_ShowMessage);
S3RE_HOOK(0x0051e160, CLogicThread_ChatInputKey, ChatInputKeyFn, Original_CLogicThread_ChatInputKey,
          CLogicThread_ChatInputKey);

// ---------------------------------------------------------------------------
// Tests: random thread images whose message lines and chat text hold
// strings on the game heap (shared, frozen or own buffers), random texts
// as arguments (sharing a line's buffer or not); PushUiFeedback is a
// recording stub. Both sides run on deep copies (each buffer copied with its
// count byte, sharing preserved); compared: the thread fields (strings by
// contents and count byte), the argument's buffer, the calls.
// ---------------------------------------------------------------------------

namespace {

constexpr std::size_t kThreadBytes = sizeof(CLogicThread);

struct Buffer {
    std::uint8_t count;
    std::vector<std::uint16_t> chars;  // with the terminating zero
};

// A string's buffer as a fresh game-heap block.
std::uint16_t* NewBuffer(const Buffer& b, std::uint32_t capacity) {
    auto* block = static_cast<std::uint16_t*>(msvc6::GameOperatorNew((capacity + 2) * 2));
    reinterpret_cast<std::uint8_t*>(block)[0] = 0;
    reinterpret_cast<std::uint8_t*>(block)[1] = b.count;
    std::memcpy(block + 1, b.chars.data(), b.chars.size() * 2);
    return block + 1;
}

// The strings of a case: 20 lines, the chat, the argument; indexes into a
// pool of buffers (-1 = none), so that strings can share one.
struct Case {
    std::vector<std::uint8_t> image;
    std::vector<Buffer> pool;
    std::vector<std::uint32_t> capacities;
    int slots[22];       // 0..19 lines, 20 chat, 21 argument
    std::uint32_t lens[22];
    std::int32_t kind;
    std::uint32_t key;
    bool chat;
};

WString* SlotString(std::uint8_t* image, WString* argument, int slot) {
    CLogicThread* t = reinterpret_cast<CLogicThread*>(image);
    if (slot < 20) {
        return TextOf(t->messages[slot]);
    }
    return slot == 20 ? &ChatOf(t)->text : argument;
}

// Builds the case's strings in `image` and `argument` (new blocks).
void Materialize(const Case& c, std::uint8_t* image, WString* argument) {
    std::vector<std::uint16_t*> made(c.pool.size(), nullptr);
    for (int s = 0; s < 22; ++s) {
        WString* w = SlotString(image, argument, s);
        const int p = c.slots[s];
        if (p < 0) {
            w->ptr = nullptr;
            w->len = 0;
            w->res = 0;
            continue;
        }
        if (made[p] == nullptr) {
            made[p] = NewBuffer(c.pool[p], c.capacities[p]);
        }
        w->ptr = made[p];
        w->len = c.lens[s];
        w->res = c.capacities[p];
    }
}

struct Snapshot {
    std::vector<std::uint32_t> fields;  // per slot: null, len, res, count byte, characters...
    std::vector<std::uint8_t> other;    // the image without the string fields
    std::vector<test::StubCall> calls;
    std::uint32_t eax = 0;
};

void Describe(std::vector<std::uint32_t>& out, const WString* w, const std::vector<const std::uint16_t*>& seen) {
    out.push_back(w->allocator);
    out.push_back(w->ptr == nullptr);
    out.push_back(w->len);
    out.push_back(w->res);
    if (w->ptr == nullptr) {
        return;
    }
    // Which earlier string shares this buffer.
    std::uint32_t shared = 0xffff;
    for (std::size_t k = 0; k < seen.size(); ++k) {
        if (seen[k] == w->ptr) {
            shared = static_cast<std::uint32_t>(k);
            break;
        }
    }
    out.push_back(shared);
    out.push_back(reinterpret_cast<const std::uint8_t*>(w->ptr)[-1]);
    for (std::uint32_t i = 0; i < w->len + 1 && i < 200; ++i) {
        out.push_back(w->ptr[i]);
    }
}

Snapshot Take(std::uint8_t* image, const WString& argumentAfter) {
    Snapshot s;
    std::vector<const std::uint16_t*> seen;
    for (int slot = 0; slot < 21; ++slot) {
        const WString* w = SlotString(image, nullptr, slot);
        Describe(s.fields, w, seen);
        seen.push_back(w->ptr);
    }
    Describe(s.fields, &argumentAfter, seen);
    s.other.assign(image, image + kThreadBytes);
    CLogicThread* t = reinterpret_cast<CLogicThread*>(s.other.data());
    for (MessageLine& line : t->messages) {
        std::memset(line.text + 4, 0, 12);  // (pointers compared above)
    }
    std::memset(s.other.data() + 0x3fc, 0, 12);
    return s;
}

Case RandomCase(test::Rng& rng) {
    Case c;
    c.image.resize(kThreadBytes);
    rng.Fill(c.image.data(), kThreadBytes);
    const int buffers = 1 + static_cast<int>(rng.Below(8));
    for (int b = 0; b < buffers; ++b) {
        Buffer buffer;
        const std::uint32_t length = rng.Below(30);
        buffer.count = static_cast<std::uint8_t>(rng.Chance(1, 2) ? 0 : (rng.Chance(1, 3) ? 0xff : rng.Below(3) + (rng.Chance(1, 8) ? 0xfc : 0)));
        for (std::uint32_t i = 0; i < length; ++i) {
            buffer.chars.push_back(static_cast<std::uint16_t>(1 + rng.Below(0xfffe)));
        }
        buffer.chars.push_back(0);
        c.pool.push_back(buffer);
        c.capacities.push_back(length + rng.Below(5));
    }
    std::vector<std::uint32_t> uses(c.pool.size(), 0);
    for (int s = 0; s < 22; ++s) {
        c.slots[s] = rng.Chance(1, 4) ? -1 : static_cast<int>(rng.Below(static_cast<std::uint32_t>(buffers)));
        const std::uint32_t length = c.slots[s] < 0 ? 0 : static_cast<std::uint32_t>(c.pool[c.slots[s]].chars.size() - 1);
        c.lens[s] = length;
        if (c.slots[s] >= 0) {
            ++uses[c.slots[s]];
        }
    }
    // Consistent reference counts (one less than the users, plus sometimes
    // references nobody holds), so no buffer is freed while still in use;
    // frozen buffers stay frozen.
    for (std::size_t b = 0; b < c.pool.size(); ++b) {
        if ((c.pool[b].count != 0xff || uses[b] > 1) && uses[b] > 0) {  // (a frozen buffer has one user)
            c.pool[b].count = static_cast<std::uint8_t>(uses[b] - 1 + (rng.Chance(1, 4) ? rng.Below(3) : 0) +
                                                        (rng.Chance(1, 10) ? 0xfc - uses[b] : 0));
        }
    }
    CLogicThread* t = reinterpret_cast<CLogicThread*>(c.image.data());
    t->message_count = rng.Chance(1, 8) ? rng.Range(1, 30) : 20;
    t->message_index = rng.Range(0, 19);
    ChatOf(t)->prompt_length = rng.Chance(1, 3) ? 0 : rng.Below(12);
    static const std::int32_t kKinds[] = {0, 5, 19, 20, 21, 100, 249, 250, 251, 253, 254};
    c.kind = rng.Chance(1, 8) ? static_cast<std::int32_t>(rng.Next32()) : kKinds[rng.Below(11)];
    c.key = rng.Chance(1, 2) ? kKeyBackspace : (rng.Chance(1, 2) ? kKeyEnter : rng.Below(300));
    return c;
}

// Runs one function on a materialized copy of the case.
Snapshot Run(const Case& c, bool chat, ShowMessageFn show, ChatInputKeyFn key) {
    std::vector<std::uint8_t> image = c.image;
    WString argument{};
    argument.allocator = static_cast<std::uint8_t>(c.kind);
    Materialize(c, image.data(), &argument);
    CLogicThread* t = reinterpret_cast<CLogicThread*>(image.data());
    test::CalleeStubs stubs;
    stubs.SetWorld(reinterpret_cast<CGameData*>(image.data()));
    stubs.Install<1, kPushUiFeedback>();
    std::uint32_t eax = 0;
    if (chat) {
        eax = key(t, nullptr, c.key) & 0xff;
    } else {
        show(t, nullptr, argument, c.kind);
    }
    // (the argument's own fields are gone with the call; its buffer is in
    // the pool and shows in the strings that share it)
    WString none{};
    Snapshot s = Take(image.data(), none);
    s.calls = stubs.Take();
    s.eax = eax;
    return s;
}

void CompareMessages(test::Context& ctx, bool chat, ShowMessageFn show, ChatInputKeyFn key, int cases) {
    for (int i = 0; i < cases; ++i) {
        const Case c = RandomCase(ctx.rng());
        const Snapshot expected = Run(c, chat, Original_CLogicThread_ShowMessage, Original_CLogicThread_ChatInputKey);
        const Snapshot actual = Run(c, chat, show, key);
        bool same = S3RE_EXPECT_EQ(expected.eax, actual.eax);
        same &= S3RE_EXPECT(expected.fields == actual.fields);
        same &= S3RE_EXPECT(expected.other == actual.other);
        same &= S3RE_EXPECT(expected.calls == actual.calls);
        if (!same) {
            for (std::size_t k = 0; k < expected.fields.size() && k < actual.fields.size(); ++k) {
                if (expected.fields[k] != actual.fields[k]) {
                    ctx.Fail(__FILE__, __LINE__, "  field %u: %x vs %x (sizes %u %u)", static_cast<unsigned>(k),
                             expected.fields[k], actual.fields[k], static_cast<unsigned>(expected.fields.size()),
                             static_cast<unsigned>(actual.fields.size()));
                    break;
                }
            }
            ctx.Fail(__FILE__, __LINE__, "  case %d (%s, kind %d, key %u)", i, chat ? "chat" : "show", c.kind, c.key);
            return;
        }
    }
}

// Wrong: kinds of exactly 250 keep 800 ticks.
void __fastcall WrongShowMessage(CLogicThread* self, void* edx, WString text, std::int32_t kind) {
    const std::int32_t index = self->message_index;
    CLogicThread_ShowMessage(self, edx, text, kind);
    if (kind == kLongLivedKinds) {
        self->messages[index].lifetime = kLifetime;
    }
}

}  // namespace

S3RE_TEST(CLogicThread_ShowMessage_MatchesOriginal) {
    CompareMessages(ctx, false, &CLogicThread_ShowMessage, &CLogicThread_ChatInputKey, 1500);
}
S3RE_TEST(CLogicThread_ShowMessage_ComparisonDetectsWrongImplementation) {
    test::Context probe(ctx.name(), 1, /*quiet=*/true);
    CompareMessages(probe, false, &WrongShowMessage, &CLogicThread_ChatInputKey, 500);
    S3RE_EXPECT(probe.failures() > 0);
}
S3RE_TEST(CLogicThread_ChatInputKey_MatchesOriginal) {
    CompareMessages(ctx, true, &CLogicThread_ShowMessage, &CLogicThread_ChatInputKey, 1500);
}

}  // namespace s3re::game

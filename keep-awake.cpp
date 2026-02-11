using namespace Argum;
using namespace std::literals;

constexpr auto g_maxDuration = std::numeric_limits<ULONGLONG>::max() / 1000;
constexpr auto g_myGuid = L"BAF0674F-E091-468A-AAA9-234909F4CFFB";

#define KA_COLOR_PID Color::bold, Color::cyan
#define KA_COLOR_DURATION Color::bold, Color::magenta
#define KA_COLOR_USER Color::bold, Color::bright_blue
#define KA_COLOR_SESSION Color::bold, Color::yellow 
#define KA_COLOR_SUCCESS Color::bold, Color::green
#define KA_COLOR_ERROR Color::bold, Color::red

#define KA_COLOR_USAGE_ARG Color::yellow
#define KA_COLOR_USAGE_COMMAND Color::green
#define KA_COLOR_USAGE_LONGOPT Color::cyan
#define KA_COLOR_USAGE_SHORTOPT Color::green

#define KA_COLOR_HELP_HEADING Color::bold, Color::blue
#define KA_COLOR_HELP_PROGNAME Color::bold, Color::magenta
#define KA_COLOR_HELP_ARG Color::bold, Color::yellow
#define KA_COLOR_HELP_COMMAND Color::bold, Color::green
#define KA_COLOR_HELP_LONGOPT Color::bold, Color::cyan
#define KA_COLOR_HELP_SHORTOPT Color::bold, Color::green

#pragma region General Win32 Utilities

static std::wstring widen(std::string_view str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), int(str.size()), nullptr, 0);
    std::wstring ret(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), int(str.size()), ret.data(), size_needed);
    return ret;
}

static std::string narrow(std::wstring_view str) {
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, str.data(), int(str.size()), nullptr, 0, nullptr, nullptr);
    std::string ret(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, str.data(), int(str.size()), ret.data(), size_needed, nullptr, nullptr);
    return ret;
}

template<HANDLE InvalidValue>
class BasicAutoHandle {
public:
    BasicAutoHandle() noexcept = default;
    BasicAutoHandle(HANDLE h) noexcept :
        m_handle(h)
    {}
    ~BasicAutoHandle() noexcept {
        if (m_handle != InvalidValue)
            CloseHandle(m_handle);
    }
    BasicAutoHandle(BasicAutoHandle && src) noexcept : 
        m_handle(std::exchange(src.m_handle, InvalidValue))
    {}
    BasicAutoHandle & operator=(BasicAutoHandle && src) noexcept {
        if (m_handle != InvalidValue)
            CloseHandle(m_handle);
        m_handle = std::exchange(src.m_handle, InvalidValue);
        return *this;
    }
    BasicAutoHandle(const BasicAutoHandle &) = delete;
    BasicAutoHandle & operator=(const BasicAutoHandle &) = delete;

    HANDLE get() const 
        { return m_handle; }
    HANDLE & out() 
        { return m_handle; }
    void reset() {
        if (m_handle != InvalidValue) {
            CloseHandle(m_handle);
            m_handle = InvalidValue;
        }
    }
    explicit operator bool() const 
        { return m_handle != InvalidValue; }
private:
    HANDLE m_handle = InvalidValue;
};

using AutoHandle = BasicAutoHandle<nullptr>;
using AutoFile = BasicAutoHandle<INVALID_HANDLE_VALUE>;

struct LocalAllocDeleter {
	void operator()(void * ptr) { LocalFree(ptr); }
};

template<class T>
requires(
	(!std::is_array_v<T> && std::is_trivially_destructible_v<T>) ||
	(std::is_array_v<T> && std::is_trivially_destructible_v<std::remove_all_extents_t<T>>)
)
using unqiue_local_membuf = std::unique_ptr<T, LocalAllocDeleter>;

//static std::wstring win32ErrorMessage(DWORD err) {
//    return widen(std::error_code(int(err), std::system_category()).message());
//}

[[noreturn]]
static inline void throwWin32Error(DWORD err, const char * doingWhat) {
    throw std::system_error(std::error_code(int(err), std::system_category()), doingWhat);
}

[[noreturn]]
static inline void throwLastError(const char * doingWhat) {
    throwWin32Error(GetLastError(), doingWhat);
}

static std::wstring myname() {
    std::wstring ret;

    DWORD size = 32;
    for( ; ; ) {
        ret.resize(size);
        auto res = GetModuleFileNameW(nullptr, ret.data(), size);
        if (res < size) {
            ret.resize(res);
            break;
        }
        auto err = GetLastError();
        if (err == ERROR_SUCCESS)
            break;
        if (err == ERROR_INSUFFICIENT_BUFFER) {
            if (err < std::numeric_limits<DWORD>::max() / 2)
                size *= 2;
            else if (err < std::numeric_limits<DWORD>::max() - 32)
                size += 32;
            else
                throw std::bad_alloc();
            continue;
        }
        throwWin32Error(err, "GetModuleFileName(nullptr)");
    }
    return ret;
}

static void getTokenInfo(HANDLE token, TOKEN_INFORMATION_CLASS infoClass, std::vector<BYTE> & buf) {

    while (true) {
        DWORD size;
        if (GetTokenInformation(token, infoClass, buf.data(), DWORD(buf.size()), &size)) {
            buf.resize(size);
            break;
        }
        auto err = GetLastError();
        if (err != ERROR_INSUFFICIENT_BUFFER)
            throwWin32Error(err, "GetTokenInformation");
        buf.resize(size);
    }
}

#pragma endregion

#pragma region Helpers

static std::wstring makePipeName(DWORD procId) {
    return std::format(L"\\\\.\\pipe\\{}-{}", g_myGuid, procId);
}

template <class... Types>
inline void wprint(FILE* const fp, const std::wformat_string<Types...> fmt, Types &&... args) {
    fputws(std::format(fmt, std::forward<Types>(args)...).c_str(), fp);
}

static inline void wprint(FILE* const fp, const std::wstring & str) {
    fputws(str.c_str(), fp);
}

static inline void wprint(FILE* const fp, const wchar_t * str) {
    fputws(str, fp);
}

template<Color First, Color... Rest>
static inline std::wstring colorize(bool enabled, std::wstring_view str) {
    if (enabled)
        return Argum::colorize<First, Rest...>(str);
    return std::wstring{str};
}

template<Color First, Color... Rest>
constexpr auto makeWColor(bool enabled) {
    if (enabled)
        return Argum::makeWColor<First, Rest...>();
    return L""sv;
}

static std::wstring formatDuration(ULONGLONG val) {
    ULONGLONG parts[4] = {};
    enum { days, hours, minutes, seconds };
    const wchar_t suffixes[std::size(parts)] = { L'd', L'h', L'm', L's' };

    parts[days]     = val / 86'400'000; val %= 86'400'000ull;
    parts[hours]    = val / 3'600'000;  val %= 3'600'000;
    parts[minutes]  = val / 60'000;     val %= 60'000;
    parts[seconds]  = val / 1000;       val %= 1000;

    parts[seconds] += (val >= 500);
    if (parts[seconds] == 60)   { ++parts[minutes]; parts[seconds] = 0; }
    if (parts[minutes] == 60)   { ++parts[hours];   parts[minutes] = 0; }
    if (parts[hours] == 24)     { ++parts[days];    parts[hours] = 0; }

    std::wstring ret;
    for (size_t i = 0; i < std::size(parts); ++i) {
        if (parts[i]) {
            ret += std::to_wstring(parts[i]);
            ret += suffixes[i];
            ret += L' ';
        }
    }
    if (ret.empty())
        return L"0s";
    ret.resize(ret.size() - 1);
    return ret;
}

static std::optional<ULONGLONG> parseDuration(std::wstring_view str) {

    auto nstr = narrow(str);

    auto m = ctre::match<
        R"_(\s*)_"
        R"_((?:([0-9]+)\s*[dD])?)_"
        R"_(\s*)_"
        R"_((?:([0-9]+)\s*[hH])?)_"
        R"_(\s*)_"
        R"_((?:([0-9]+)\s*[mM])?)_"
        R"_(\s*)_"
        R"_((?:([0-9]+)\s*[sS]?)?)_"
        R"_(\s*)_">(nstr);

    if (!m)
        return {};

    
    auto parsePart = [](std::string_view digits, unsigned multiple, ULONGLONG & acc) {
        auto first = digits.data();
        auto last = first + digits.size();

        ULONGLONG val;
        auto [ptr, ec] = std::from_chars(first, last, val, 10);
        if (ec != std::errc() || ptr != last)
            return false;
        if ((g_maxDuration - acc) / multiple < val)
            return false;
        acc += val * multiple;
        return true;
    };

    ULONGLONG acc = 0;
    bool has_value = false;
    if (auto days = m.get<1>()) {
        if (!parsePart(days.to_view(), 86'400, acc))
            return std::numeric_limits<ULONGLONG>::max();
        has_value = true;
    }
    if (auto hours = m.get<2>()) {
        if (!parsePart(hours.to_view(), 3'600, acc))
            return std::numeric_limits<ULONGLONG>::max();
        has_value = true;
    }
    if (auto minutes = m.get<3>()) {
        if (!parsePart(minutes.to_view(), 60, acc))
            return std::numeric_limits<ULONGLONG>::max();
        has_value = true;
    }
    if (auto seconds = m.get<4>()) {
        if (!parsePart(seconds.to_view(), 1, acc))
            return std::numeric_limits<ULONGLONG>::max();
        has_value = true;
    }

    if (!has_value)
        return {};

    acc *= 1000;
    return acc;
}

#pragma endregion

#pragma region Child Process Code

class WaitTracker {
public:
    WaitTracker(std::optional<ULONGLONG> duration): 
        m_duration(duration),
        m_start(GetTickCount64())
    {}

    bool isDone() {
        return m_duration && (GetTickCount64() - m_start) >= *m_duration;
    }

    bool waitNext(HANDLE h) {
        while (true) {
            DWORD wait_time = 3'600'000;
            if (m_duration) {
                auto elapsed = GetTickCount64() - m_start;
                if (elapsed >= *m_duration)
                    return false;
                wait_time = DWORD(std::min(ULONGLONG(wait_time), *m_duration - elapsed));
            }
            auto res = WaitForSingleObject(h, wait_time);
            if (res == WAIT_TIMEOUT)
                continue;
            if (res == WAIT_OBJECT_0)
                return true;
            return false;
        }
    }

    std::wstring formatRemaining() const {
        if (!m_duration)
            return L"Infinite";
        auto elapsed = GetTickCount64() - m_start;
        ULONGLONG remaining = (elapsed <= *m_duration ? *m_duration - elapsed : 0);
        return formatDuration(remaining);
    }
private:
    std::optional<ULONGLONG> m_duration;
    ULONGLONG m_start;
};


static void writeInfo(HANDLE hPipe, const WaitTracker & tracker) {
    std::string reply = narrow(tracker.formatRemaining());
    auto size = DWORD(reply.size());
    for(DWORD total = 0; total < size; ) {
        DWORD written;
        if (!WriteFile(hPipe, (const BYTE *)reply.data() + total, size - total, &written, nullptr))
            break;
        total += written;
    }
    FlushFileBuffers(hPipe);
}

// By default pipes get a security descriptor that grants read access to 
// members of the Everyone group and the anonymous account. Ugh
// Let's create one that doesn't
static unqiue_local_membuf<SECURITY_DESCRIPTOR> createPipeSecurityDescriptor() {
    HANDLE token = GetCurrentProcessToken();

    std::vector<BYTE> buf;
    getTokenInfo(token, TokenUser, buf);
    auto pusr = (TOKEN_USER *)buf.data();
    unqiue_local_membuf<wchar_t> stringUserSid;
    if (!ConvertSidToStringSidW(pusr->User.Sid, std::out_ptr(stringUserSid)))
        throwLastError("ConvertSidToStringSid");

    getTokenInfo(token, TokenPrimaryGroup, buf);
    auto * pgrp = (TOKEN_PRIMARY_GROUP *)buf.data();
    unqiue_local_membuf<wchar_t> stringGroupSid;
    if (!ConvertSidToStringSidW(pgrp->PrimaryGroup, std::out_ptr(stringGroupSid)))
        throwLastError("ConvertSidToStringSid");

    auto sddl = std::format(L"O:{0}G:{1}D:(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;{0})", stringUserSid.get(), stringGroupSid.get());

    unqiue_local_membuf<SECURITY_DESCRIPTOR> desc;
    ULONG descSize;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptor(sddl.c_str(), SDDL_REVISION_1, std::out_ptr(desc), &descSize))
        throwLastError("ConvertStringSecurityDescriptorToSecurityDescriptor");

    return desc;
}

static void runDirect(std::optional<ULONGLONG> duration, ColorStatus envColorStatus) {

    auto desc = createPipeSecurityDescriptor();

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = desc.get();
    sa.bInheritHandle = false;

    AutoFile hPipe =  CreateNamedPipe(makePipeName(GetCurrentProcessId()).c_str(),
                                      PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED, 
                                      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                      1, 4096, 0, 
                                      NMPWAIT_USE_DEFAULT_WAIT, &sa);
    if (!hPipe)
        throwLastError("CreateNamedPipe");

    AutoHandle hEvent = CreateEvent(nullptr, true, true, nullptr);
    if (!hEvent)
        throwLastError("CreateEvent");

        
    auto oldState = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    if (oldState == 0)
        throwLastError("SetThreadExecutionState");

    auto useColor = shouldUseColor(envColorStatus, stdout);

    if (duration) 
        wprint(stdout, L"{0}preventing sleep for{1} {2}{4}{1} {0}or until process{1} {3}{5}{1} {0}is stopped{1}\n",
            makeWColor<KA_COLOR_SUCCESS>(useColor),
            makeWColor<Color::normal>(useColor),
            makeWColor<KA_COLOR_DURATION>(useColor),
            makeWColor<KA_COLOR_PID>(useColor),
            formatDuration(*duration), 
            GetCurrentProcessId());
    else
        wprint(stdout, L"{0}preventing sleep indefinitely or until process{1} {2}{3}{1} {0}is stopped{1}\n",
            makeWColor<KA_COLOR_SUCCESS>(useColor),
            makeWColor<Color::normal>(useColor),
            makeWColor<KA_COLOR_PID>(useColor),
            GetCurrentProcessId());
    
    //Disconnect from parent, exceptions will not be reported from this point on
    (void)freopen("NUL:", "w", stdout);
    (void)freopen("NUL:", "w", stderr);
        
    WaitTracker tracker(duration);
        
    while(!tracker.isDone()) {

        OVERLAPPED ovl{};
        ovl.hEvent = hEvent.get();

        // The code below can work with or without pipe
        // For now we fail above if the pipe cannot be created.
        // However, it is possible to make that non-fatal and this
        //code will work just fine.
        SetLastError(ERROR_IO_PENDING);
        if (!hPipe || !ConnectNamedPipe(hPipe.get(), &ovl)) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                if (!tracker.waitNext(hEvent.get()))
                    break;
                DWORD dummy;
                GetOverlappedResult(hPipe.get(), &ovl, &dummy, false);
            } else if (err != ERROR_PIPE_CONNECTED) {
                DisconnectNamedPipe(hPipe.get());
                continue;
            }
        }
        assert(hPipe);
                
        std::string command(4, '\0');
        DWORD read;
        if (ReadFile(hPipe.get(), command.data(), DWORD(command.size()), &read, nullptr) && read == 4) {
            if (command == "info") {
                writeInfo(hPipe.get(), tracker);
            } else if (command == "stop") {
                break;
            }
        }
        DisconnectNamedPipe(hPipe.get());
    }
    
    SetThreadExecutionState(ES_CONTINUOUS);
        
}

#pragma endregion

#pragma region Main Code

static void runChild(ColorStatus envColorStatus) {
    if (!SetEnvironmentVariable(g_myGuid, L"ON"))
        throwLastError("SetEnvironmentVariable");

    if (shouldUseColor(envColorStatus, stdout) && shouldUseColor(envColorStatus, stderr)) {
        if (!SetEnvironmentVariable(L"FORCE_COLOR", L"1"))
            throwLastError("SetEnvironmentVariable");
    }

    std::wstring exe = myname();
    
    SECURITY_ATTRIBUTES pipeAttr;
    pipeAttr.nLength = sizeof(pipeAttr); 
    pipeAttr.bInheritHandle = true; 
    pipeAttr.lpSecurityDescriptor = nullptr; 

    AutoFile hRead, hWrite;
    if (!CreatePipe(&hRead.out(), &hWrite.out(), &pipeAttr, 0))
        throwLastError("CreatePipe");
    if (!SetHandleInformation(hRead.get(), HANDLE_FLAG_INHERIT, 0))
        throwLastError("SetHandleInformation(read pipe)");
    AutoFile hErrWrite;
    if (!DuplicateHandle(GetCurrentProcess(), hWrite.get(), GetCurrentProcess(), &hErrWrite.out(), 0, true, DUPLICATE_SAME_ACCESS))
        throwLastError("DuplicateHandle(write pipe)");

    for(DWORD id: {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE}) {
        auto h = GetStdHandle(id);
        if (h == nullptr || h == INVALID_HANDLE_VALUE)
            continue;
        if (!SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0))
            throwLastError("SetHandleInformation(std handle)");
    }
    
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = hWrite.get();
    si.hStdError = hErrWrite.get();
    PROCESS_INFORMATION pi;

    if (!CreateProcess(exe.data(), GetCommandLine(), nullptr, nullptr, true, CREATE_DEFAULT_ERROR_MODE | CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi))
        throwLastError("CreateProcess");
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    hWrite.reset();
    hErrWrite.reset();
    while (true) {
        char buf[4096];
        DWORD read;
        if (!ReadFile(hRead.get(), buf, sizeof(buf), &read, nullptr))
            break;
        fwrite(buf, 1, read, stdout);
    }
}

static DWORD execOnPipe(DWORD procId, std::string_view cmd, std::invocable<HANDLE> auto && proc) 
    requires(std::is_same_v<decltype(std::forward<decltype(proc)>(proc)(HANDLE{})), DWORD> ||
             std::is_same_v<decltype(std::forward<decltype(proc)>(proc)(HANDLE{})), void>)
{
    std::wstring pipeName = makePipeName(procId);
    while(true) {
        AutoFile hPipe = CreateFile(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (!hPipe) {
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_BUSY) {
                if (!WaitNamedPipe(pipeName.c_str(), NMPWAIT_WAIT_FOREVER))
                    return err;
                continue;
            }
            return err;
        }
        DWORD written;
        if (!WriteFile(hPipe.get(), cmd.data(), DWORD(cmd.size()), &written, nullptr) || written != cmd.size())
            return GetLastError();
        if constexpr (std::is_same_v<decltype(std::forward<decltype(proc)>(proc)(hPipe.get())), DWORD>)
            return std::forward<decltype(proc)>(proc)(hPipe.get());
        else {
            std::forward<decltype(proc)>(proc)(hPipe.get());
            return ERROR_SUCCESS;
        }
    }
}

static std::optional<std::wstring> getInfo(DWORD procId) {
    std::string buf(256, '\0');
    auto err = execOnPipe(procId, "info", [&buf](HANDLE hPipe) -> DWORD {
        DWORD read;
        if (!ReadFile(hPipe, buf.data(), DWORD(buf.size()), &read, nullptr))
            return GetLastError();
        buf.resize(read);
        return ERROR_SUCCESS;
    }); 
    if (err == ERROR_SUCCESS)
        return widen(buf);
    if (err == ERROR_ACCESS_DENIED)
        return L"<inaccessible>";
    return {};
}

static bool kill(DWORD procId) {
    return execOnPipe(procId, "stop", [] (HANDLE) {}) == ERROR_SUCCESS;
}

static std::wstring sidToUsername(PSID psid) {

    if (!psid)
        return L"<unknown>";

    std::wstring name, domain;

    while (true) {
        DWORD nameSize = DWORD(name.size()), domainSize = DWORD(domain.size());
        SID_NAME_USE use;
        if (LookupAccountSidW(nullptr, psid, name.data(), &nameSize, domain.data(), &domainSize, &use)) {
            name.resize(wcslen(name.c_str()));
            domain.resize(wcslen(domain.c_str()));
            break;
        }
        auto err = GetLastError();
        if (err != ERROR_INSUFFICIENT_BUFFER) {
            unqiue_local_membuf<wchar_t> stringSid;
            if (!ConvertSidToStringSidW(psid, std::out_ptr(stringSid)))
                return L"<unknown>";
            return stringSid.get();
        }
        name.resize(nameSize);
        domain.resize(domainSize);
    }
    return domain + L'\\' + name;
}

static void listProcesses(ColorStatus envColorStatus) {
    WTS_PROCESS_INFO * pi;
    DWORD count;
    if (!WTSEnumerateProcesses(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pi, &count))
        throwLastError("WTSEnumerateProcesses");

    size_t widths[4] = {9, 16, 4, 16};
    enum Align {
        left,
        right
    } aligns[std::size(widths)] = {right, left, right, right};
    using Row = std::array<std::wstring, std::size(widths)>;
    std::vector<Row> table;

    auto addRow = [&](Row && row) {
        for(size_t i = 0; i < row.size(); ++i) {
            widths[i] = std::max(widths[i], size_t(stringWidth(row[i])));
        }
        table.push_back(std::move(row));
    };

    auto useColor = shouldUseColor(envColorStatus, stdout);
    

    addRow({
        colorize<KA_COLOR_PID>(useColor, L"PID"), 
        colorize<KA_COLOR_USER>(useColor, L"USER"), 
        colorize<KA_COLOR_SESSION>(useColor, L"SESSION"), 
        colorize<KA_COLOR_DURATION>(useColor, L"REMAINING")
    });
    auto mypid = GetCurrentProcessId();
    for(DWORD i = 0; i < count; ++i) {
        auto & info = pi[i];
        if (pi[i].ProcessId == mypid)
            continue;
        if (info.pProcessName == L"keep-awake.exe"sv) {
            if (auto remaining = getInfo(pi[i].ProcessId)) {
                addRow({
                    colorize<KA_COLOR_PID>(useColor, std::to_wstring(pi[i].ProcessId)), 
                    colorize<KA_COLOR_USER>(useColor, sidToUsername(pi[i].pUserSid)), 
                    colorize<KA_COLOR_SESSION>(useColor, std::to_wstring(pi[i].SessionId)), 
                    colorize<KA_COLOR_DURATION>(useColor, *remaining)});
            }
        }
    }

    for (auto & row: table) {
        std::wstring acc;
        for(size_t i = 0; i < row.size(); ++i) {
            std::wstring padding(widths[i] - stringWidth(row[i]), L' ');
            if (aligns[i] == left)
                acc += row[i] + padding;
            else
                acc += padding + row[i];
            acc += L"  ";
        }
        if (!acc.empty())
            acc.resize(acc.size() - 2);
        acc += L'\n';
        wprint(stdout, acc);
    }
}

static void normalizeStdIO() noexcept {

    std::tuple<DWORD, int> stdHandles[] = {
        {STD_OUTPUT_HANDLE, 1},
        {STD_ERROR_HANDLE, 2}
    };

    for(auto [id, desc]: stdHandles) {
        auto handle = GetStdHandle(id);
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE && HANDLE(_get_osfhandle(desc)) != handle) {
        
            HANDLE uniqueHandle = INVALID_HANDLE_VALUE;
            if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &uniqueHandle, 0, true, DUPLICATE_SAME_ACCESS))
                continue;
            auto fd = _open_osfhandle(intptr_t(uniqueHandle), 0);
            if (fd < 0) {
                CloseHandle(uniqueHandle);
                continue;
            }
            auto dupres = _dup2(fd, desc);
            _close(fd);
            if (dupres != 0)
                continue;
            SetStdHandle(id, uniqueHandle);
            CloseHandle(handle);
        }
    }

}

struct Layout : WHelpFormatter::Layout {
    Layout(FILE * fp) {
        this->width = terminalWidth(fp);

        auto minWidth = this->helpLeadingGap + this->helpNameMaxWidth + this->helpDescriptionGap;

        if (this->width <= minWidth)
            this->width = minWidth + 1;
    }

    unsigned usageLeadingSpace = 2;
};

static std::wstring formatLine(std::wstring_view line, 
                               const Layout & layout, 
                               unsigned leadingSpace = 0) {
    std::wstring ret = wordWrap(std::wstring(leadingSpace, L' ') + std::wstring(line), 
                                layout.width, leadingSpace + layout.helpLeadingGap);
    ret += L"\n";
    return ret;
}

static std::wstring formatItemHelp(std::wstring_view name, 
    std::wstring_view description,
    unsigned maxNameLen,
    const Layout & layout) {
    auto descColumnOffset = layout.helpLeadingGap + maxNameLen + layout.helpDescriptionGap;

    std::wstring ret = wordWrap(std::wstring(layout.helpLeadingGap, L' ').append(name), layout.width, layout.helpLeadingGap);
    auto lastEndlPos = ret.rfind(L'\n');
    auto lastLineLen = stringWidth(std::wstring_view(ret.c_str() + (lastEndlPos + 1), ret.size() - (lastEndlPos + 1)));

    if (lastLineLen > maxNameLen + layout.helpLeadingGap) {
        ret += L'\n';
        ret.append(descColumnOffset, L' ');
    } else {
        ret.append(descColumnOffset - lastLineLen, L' ');
    }

    ret.append(wordWrap(description, layout.width, descColumnOffset, descColumnOffset));
    ret += L'\n';

    return ret;
}

static std::wstring usage(const wchar_t * progname, const Layout & layout, bool useColor) {
    std::wstring ret = colorize<KA_COLOR_HELP_HEADING>(useColor, L"Usage:\n");
    auto colprogname = colorize<KA_COLOR_HELP_PROGNAME>(useColor, progname);
    ret += formatLine(std::format(L"{0} [{1}duration{2}]",
                                  colprogname,
                                  makeWColor<KA_COLOR_USAGE_ARG>(useColor),
                                  makeWColor<Color::normal>(useColor)),
                      layout, layout.usageLeadingSpace);
    ret += formatLine(std::format(L"{0} {1}list{2}",
                                  colprogname,
                                  makeWColor<KA_COLOR_USAGE_COMMAND>(useColor),
                                  makeWColor<Color::normal>(useColor)),
                      layout, layout.usageLeadingSpace);
    ret += formatLine(std::format(L"{0} {1}stop{2} {3}pid{2} [{3}pid{2} ...]",
                                  colprogname,
                                  makeWColor<KA_COLOR_USAGE_COMMAND>(useColor),
                                  makeWColor<Color::normal>(useColor),
                                  makeWColor<KA_COLOR_USAGE_ARG>(useColor)),
                      layout, layout.usageLeadingSpace);
    ret += formatLine(std::format(L"{0} {1}--help{2}|{3}-h{2}",
                                  colprogname,
                                  makeWColor<KA_COLOR_USAGE_LONGOPT>(useColor),
                                  makeWColor<Color::normal>(useColor),
                                  makeWColor<KA_COLOR_USAGE_SHORTOPT>(useColor)),
                      layout, layout.usageLeadingSpace);
    ret += formatLine(std::format(L"{0} {1}--version{2}",
                                  colprogname,
                                  makeWColor<KA_COLOR_USAGE_LONGOPT>(useColor),
                                  makeWColor<Color::normal>(useColor)),
                      layout, layout.usageLeadingSpace);
    return ret;
}


static std::wstring help(const wchar_t * argv0, const Layout & layout, bool useColor) {
    unsigned maxNameLength = 18;
    
    if (maxNameLength > layout.helpNameMaxWidth)
        maxNameLength = layout.helpNameMaxWidth;

    std::wstring ret = usage(argv0, layout, useColor);
    ret += L"\n";
    
    ret += formatLine(colorize<KA_COLOR_HELP_HEADING>(useColor, L"arguments:"), layout);
    ret += formatItemHelp(colorize<KA_COLOR_HELP_ARG>(useColor, L"duration"), 
                          L"how long to keep computer awake in milliseconds. If omitted, keep it awake indefinitely.", 
                          maxNameLength, layout);
    ret += L"\n";
    
    ret += formatLine(colorize<KA_COLOR_HELP_HEADING>(useColor, L"commands:"), layout);
    ret += formatItemHelp(colorize<KA_COLOR_HELP_COMMAND>(useColor, L"list"), 
                          L"show info about currently active keep-awake instances.",
                          maxNameLength, layout);
    ret += formatItemHelp(std::format(L"{0}stop{1} {2}pid{1} [{2}pid{1} ...]",
                                      makeWColor<KA_COLOR_HELP_COMMAND>(useColor),
                                      makeWColor<Color::normal>(useColor),
                                      makeWColor<KA_COLOR_HELP_ARG>(useColor)),
                          std::format(L"stop keep-awake instances given by {0}pid{1} arguments.",
                                      makeWColor<KA_COLOR_HELP_ARG>(useColor),
                                      makeWColor<Color::normal>(useColor)),
                          maxNameLength, layout);
    ret += L"\n";
    
    ret += formatLine(colorize<KA_COLOR_HELP_HEADING>(useColor, L"options:"), layout);
    ret += formatItemHelp(std::format(L"{0}--help{1}, {2}-h{1}",
                                      makeWColor<KA_COLOR_HELP_LONGOPT>(useColor),
                                      makeWColor<Color::normal>(useColor),
                                      makeWColor<KA_COLOR_HELP_SHORTOPT>(useColor)),
                          L"show this help message and exit.",
                          maxNameLength, layout);
    ret += formatItemHelp(std::format(L"{0}--version{1}",
                                      makeWColor<KA_COLOR_HELP_LONGOPT>(useColor),
                                      makeWColor<Color::normal>(useColor)), 
                          L"report app version and exit.",
                          maxNameLength, layout);
    ret += L"\n";
    
    return ret;
}

int wmain(int argc, wchar_t * argv[]) {

    normalizeStdIO();

    const ColorStatus envColorStatus = environmentColorStatus();

    const auto myenv = _wgetenv(g_myGuid);
    const bool isChild = myenv && myenv == L"ON"sv;
    const auto progname = argc ? argv[0] : L"keep-awake";

    std::optional<ULONGLONG> duration;
    std::optional<std::wstring> command;
    std::vector<DWORD> pidsToKill;

    WParser parser;
    try {
        parser.add(WOption(L"--help", L"-h").handler(
            [&]() {
                wprint(stdout, help(progname, Layout(stdout), shouldUseColor(envColorStatus, stdout)));
                std::exit(EXIT_SUCCESS);
        }));
        parser.add(WOption(L"--version").handler(
            [&]() {
                wprint(stdout, L"" KEEP_AWAKE_VERSION);
                std::exit(EXIT_SUCCESS);
        }));
        parser.add(WPositional(L"command").occurs(neverOrOnce).handler(
            [&](const std::wstring_view & value) -> WExpected<void> {

                if (value == L"list" || value == L"stop") {
                    command = value;
                } else if (auto maybeVal = parseDuration(value)) {
                    auto val = *maybeVal;
                    if (val > 86'400'000 * 365ull)
                        return {Failure<WParser::ValidationError>, 
                                std::format(L"duration \"{}\" is too large (do you really want to prevent sleep for more than a year?)", value)};
                    duration = val;
                } else {
                    return {Failure<WParser::UnrecognizedOption>, value};
                }
                return {};
        }));
        parser.add(WPositional(L"args").occurs(zeroOrMoreTimes).handler(
            [&](const std::wstring_view & value) -> WExpected<void> {

                if (command && *command == L"stop") {
                    pidsToKill.push_back(parseIntegral<DWORD>(value).value());
                    return {};
                } 
                    
                return {Failure<WParser::ExtraPositional>, value};                
        }));
        parser.addValidator([&](const WValidationData & ) {
            return !command || *command != L"stop" || !pidsToKill.empty();
        }, L"stop command requires PID arguments");
        
        if (auto err = parser.parse(argc, argv).error()) {
            auto useColor = shouldUseColor(envColorStatus, stderr);
            wprint(stderr, L"{}\n\n{}\n", 
                   colorize<KA_COLOR_ERROR>(useColor, err->message()), 
                   usage(progname, Layout(stderr), useColor));
            return EXIT_FAILURE;
        }
        

        if (command) {
            if (*command == L"list") {
                listProcesses(envColorStatus);
                return EXIT_SUCCESS;
            } 

            assert(*command == L"stop");
            assert(!pidsToKill.empty());
            for(auto pid: pidsToKill) {
                auto useColor = shouldUseColor(envColorStatus, stdout);
                if (kill(pid))
                    wprint(stdout, L"{0}stop request successfully sent to process{1} {2}{3}{1}\n",
                        makeWColor<KA_COLOR_SUCCESS>(useColor),
                        makeWColor<Color::normal>(useColor),
                        makeWColor<KA_COLOR_PID>(useColor),
                        pid);
                else
                    wprint(stdout, L"{0}unable to send stop request to process{1} {2}{3}{1}\n",
                        makeWColor<KA_COLOR_ERROR>(useColor),
                        makeWColor<Color::normal>(useColor),
                        makeWColor<KA_COLOR_PID>(useColor),
                        pid);
            }
            return EXIT_SUCCESS;
        }
        
        if (isChild)
            runDirect(duration, envColorStatus);
        else
            runChild(envColorStatus);

        return EXIT_SUCCESS;

    } catch (std::exception & ex) {
        auto colorizer = wideColorizerForFile(envColorStatus, stderr);
        auto message = widen(ex.what());
        if (isChild)
            wprint(stderr, colorizer.error(std::format(L"{}: (child): {}", progname, message)));
        else
            wprint(stderr, colorizer.error(std::format(L"{}: {}", progname, message)));
    }
    return EXIT_FAILURE;
}

#pragma endregion

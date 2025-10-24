using namespace Argum;
using namespace std::literals;

constexpr auto g_maxDuration = std::numeric_limits<ULONGLONG>::max() / 1000;
constexpr auto g_myGuid = L"BAF0674F-E091-468A-AAA9-234909F4CFFB";


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

static std::wstring myname() {
    std::wstring ret;

    DWORD size = 32;
    for( ; ; ) {
        ret.resize(size);
        auto res = GetModuleFileNameW(0, ret.data(), size);
        if (res < size) {
            ret.resize(res);
            break;
        }
        auto err = GetLastError();
        if (err == NO_ERROR)
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
        throw std::system_error(std::error_code(err, std::system_category()));
    }
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

static std::wstring makePipeName(DWORD procId) {
    return std::format(L"\\\\.\\pipe\\{}-{}", g_myGuid, procId);
}


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

static void runDirect(std::optional<ULONGLONG> duration) {

    //Sleep(15000);
    
    try {

        AutoFile hPipe =  CreateNamedPipeW(makePipeName(GetCurrentProcessId()).c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED, 
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES, 4096, 0, 
            NMPWAIT_USE_DEFAULT_WAIT, nullptr);
        if (!hPipe) {
            fputws(std::format(L"child process: error creating pipe: {}", widen(std::error_code(int(GetLastError()), std::system_category()).message())).c_str(), stderr);
        }

        AutoHandle hEvent = CreateEvent(nullptr, true, true, nullptr);
        if (!hEvent)
            throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));

        
        auto oldState = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
        if (oldState == 0)
            throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));

        fputws((duration ? 
                std::format(L"preventing sleep for {0} or untill process {1} is stopped\n", formatDuration(*duration), GetCurrentProcessId()) : 
                std::format(L"preventing sleep indefinitely or untill process {0} is stopped\n", GetCurrentProcessId())).c_str(),
               stdout);

        (void)freopen("NUL:", "w", stdout);
        (void)freopen("NUL:", "w", stderr);
        
        WaitTracker tracker(duration);
        
        while(!tracker.isDone()) {

            OVERLAPPED ovl{};
            ovl.hEvent = hEvent.get();

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
        

    } catch(WParsingException & ex) {
        std::wstring message = L"child process error: ";
        message += ex.message();
        fputws(message.c_str(), stderr);
    } catch (std::exception & ex) {
        std::wstring message = L"child process error: ";
        message += widen(ex.what());
        fputws(message.c_str(), stderr);
    }
}


static void runChild() {
    if (!SetEnvironmentVariable(g_myGuid, L"ON"))
        throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));

    std::wstring exe = myname();
    
    SECURITY_ATTRIBUTES pipeAttr;
    pipeAttr.nLength = sizeof(pipeAttr); 
    pipeAttr.bInheritHandle = true; 
    pipeAttr.lpSecurityDescriptor = nullptr; 

    AutoFile hRead, hWrite;
    if (!CreatePipe(&hRead.out(), &hWrite.out(), &pipeAttr, 0))
        throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));
    if (!SetHandleInformation(hRead.get(), HANDLE_FLAG_INHERIT, 0))
        throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));
    AutoFile hErrWrite;
    if (!DuplicateHandle(GetCurrentProcess(), hWrite.get(), GetCurrentProcess(), &hErrWrite.out(), 0, true, DUPLICATE_SAME_ACCESS))
        throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));

    if (!SetHandleInformation(GetStdHandle(STD_INPUT_HANDLE), HANDLE_FLAG_INHERIT, 0))
        throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));
    if (!SetHandleInformation(GetStdHandle(STD_OUTPUT_HANDLE), HANDLE_FLAG_INHERIT, 0))
        throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));
    if (!SetHandleInformation(GetStdHandle(STD_ERROR_HANDLE), HANDLE_FLAG_INHERIT, 0))
        throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));


    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = hWrite.get();
    si.hStdError = hErrWrite.get();
    PROCESS_INFORMATION pi;

    if (!CreateProcess(exe.data(), GetCommandLine(), nullptr, nullptr, true, CREATE_DEFAULT_ERROR_MODE | CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi))
        throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));
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

static bool execOnPipe(DWORD procId, std::string_view cmd, std::invocable<HANDLE> auto && proc) {
    std::wstring pipeName = makePipeName(procId);
    while(true) {
        AutoFile hPipe = CreateFile(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (!hPipe) {
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_BUSY) {
                if (!WaitNamedPipe(pipeName.c_str(), NMPWAIT_WAIT_FOREVER))
                    return false;
                continue;
            }
        }
        DWORD written;
        if (!WriteFile(hPipe.get(), cmd.data(), DWORD(cmd.size()), &written, nullptr) || written != cmd.size())
            return false;
        if constexpr (std::is_convertible_v<decltype(std::forward<decltype(proc)>(proc)(hPipe.get())), bool>)
            return std::forward<decltype(proc)>(proc)(hPipe.get());
        else {
            std::forward<decltype(proc)>(proc)(hPipe.get());
            return true;
        }
    }
}

static std::optional<std::wstring> getInfo(DWORD procId) {
    std::string buf(256, '\0');
    if (!execOnPipe(procId, "info", [&buf](HANDLE hPipe) {
            DWORD read;
            if (!ReadFile(hPipe, buf.data(), DWORD(buf.size()), &read, nullptr))
                return false;
            buf.resize(read);
            return true;
        }))
        return {};

    return widen(buf);
}

static bool kill(DWORD procId) {
    return execOnPipe(procId, "stop", [] (HANDLE) {});
}

static std::wstring sidToUsername(PSID psid) {
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

static void listProcesses() {
    WTS_PROCESS_INFO * pi;
    DWORD count;
    if (!WTSEnumerateProcesses(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pi, &count))
        throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));

    size_t widths[4] = {9, 16, 7, 10};
    enum Align {
        left,
        right
    } aligns[std::size(widths)] = {right, left, right, right};
    using Row = std::array<std::wstring, std::size(widths)>;
    std::vector<Row> table;

    auto addRow = [&](Row && row) {
        for(size_t i = 0; i < row.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].size());
        }
        table.push_back(std::move(row));
    };
    

    addRow({L"PID", L"USER", L"SESSION", L"REMAINING"});
    for(DWORD i = 0; i < count; ++i) {
        auto & info = pi[i];
        if (info.pProcessName == L"keep-awake.exe"sv) {
            if (auto remaining = getInfo(pi[i].ProcessId)) {
                addRow({std::to_wstring(pi[i].ProcessId), sidToUsername(pi[i].pUserSid), std::to_wstring(pi[i].SessionId), *remaining});
            }
        }
    }

    for (auto & row: table) {
        for(size_t i = 0; i < row.size(); ++i) {
            std::wstring padding(widths[i] - row[i].size(), L' ');
            if (aligns[i] == left)
                row[i] = row[i] + padding;
            else
                row[i] = padding + row[i];
        }
    }

    for (auto & row: table) {
        fputws(std::format(L"{}  {}  {}  {}\n", row[0], row[1], row[2], row[3]).c_str(), stdout);
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

static std::wstring usage(const wchar_t * progname) {
    return std::format(
LR"_(Usage: 
    {0} [duration] 
    {0} list
    {0} stop pid [pid ...]
    {0} --help
)_", progname);
}

static std::wstring help(const wchar_t * argv0) {
    std::wstring ret = usage(argv0);
    ret += 
LR"_(
positional arguments:
  duration    how long to keep computer awake in milliseconds.
              If omitted, keep it awake indefinitely.
  list        show info about currently active keep-awake instances
  stop        stop keep-awake instances given by pid arguments

options:
  --help, -h  show this help message and exit
)_";
    return ret;
}

int wmain(int argc, wchar_t * argv[]) {


    //Sleep(15000);

    normalizeStdIO();

    auto myenv = _wgetenv(g_myGuid);
    bool isChild = myenv && myenv == L"ON"sv;
    auto progname = argc ? argv[0] : L"keep-awake";

    std::optional<ULONGLONG> duration;
    std::optional<std::wstring> command;
    std::vector<DWORD> pidsToKill;

    WParser parser;
    try {
        parser.add(
            WOption(L"--help", L"-h"). 
            help(L"show this help message and exit"). 
            handler([&]() {  

                fputws(help(progname).c_str(), stdout);
                std::exit(EXIT_SUCCESS);
        }));
        parser.add(
            WPositional(L"command").
            occurs(neverOrOnce).
            handler([&](const std::wstring_view & value) -> WExpected<void> {
                if (value == L"list" || value == L"stop") {
                    command = value;
                } else if (auto maybeVal = parseDuration(value)) {
                    auto val = *maybeVal;
                    if (val > 86'400'000 * 365ull)
                        return {Failure<WParser::ValidationError>, std::format(L"duration \"{}\" is too large (do you really want to prevent sleep for more than a year?)", value)};
                    duration = val;
                } else {
                    return {Failure<WParser::UnrecognizedOption>, value};
                }
                return {};
        }));
        parser.add(
            WPositional(L"args").
            occurs(zeroOrMoreTimes).
            handler([&](const std::wstring_view & value) -> WExpected<void> {

                if (command && *command == L"stop") {
                    pidsToKill.push_back(parseIntegral<DWORD>(value).value());
                    return {};
                } 
                    
                return {Failure<WParser::ExtraPositional>, value};                
        }));
        parser.addValidator([&](const WValidationData & ) {
            return !command || *command != L"stop" || !pidsToKill.empty();
        }, L"stop command requires PID arguments");
        
        auto result = parser.parse(argc, argv);
        if (auto err = result.error()) {
            std::wstring message;
            message += err->message();
            message += L"\n\n";
            message += usage(progname);
            message += '\n';
            fputws(message.c_str(), stderr);
            return EXIT_FAILURE;
        }
        

        if (command) {
            if (*command == L"list") {
                listProcesses();
                return EXIT_SUCCESS;
            } 

            assert(*command == L"stop");
            assert(!pidsToKill.empty());
            for(auto pid: pidsToKill) {
                if (kill(pid))
                    fputws(std::format(L"stop request successfully sent to process {}\n", pid).c_str(), stdout);
                else
                    fputws(std::format(L"unable to send stop request to process {}\n", pid).c_str(), stdout);
            }
            return EXIT_SUCCESS;
        }
        
        if (isChild)
            runDirect(duration);
        else
            runChild();

        return EXIT_SUCCESS;

    } catch (std::exception & ex) {
        fputws(std::format(L"{}: {}", progname, widen(ex.what())).c_str(), stderr);
    }
    return EXIT_FAILURE;
}




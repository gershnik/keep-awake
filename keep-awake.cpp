using namespace Argum;
using namespace std::literals;


std::wstring widen(std::string_view str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), int(str.size()), nullptr, 0);
    std::wstring ret(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), int(str.size()), ret.data(), size_needed);
    return ret;
}

std::string formatDuration(ULONGLONG val) {
    ULONGLONG parts[4] = {};
    enum { days, hours, minutes, seconds };
    const char suffixes[std::size(parts)] = { 'd', 'h', 'm', 's' };

    parts[days]     = val / 86'400'000; val %= 86'400'000ull;
    parts[hours]    = val / 3'600'000;  val %= 3'600'000;
    parts[minutes]  = val / 60'000;     val %= 60'000;
    parts[seconds]  = val / 1000;       val %= 1000;

    parts[seconds] += (val >= 500);
    if (parts[seconds] == 60)   { ++parts[minutes]; parts[seconds] = 0; }
    if (parts[minutes] == 60)   { ++parts[hours];   parts[minutes] = 0; }
    if (parts[hours] == 24)     { ++parts[days];    parts[hours] = 0; }

    size_t i = 0;
    while(i < std::size(parts) - 1 && parts[i] == 0)
        ++i;
    
    std::string ret;
    for ( ; i < std::size(parts); ++i) {
        ret += std::to_string(parts[i]);
        ret += suffixes[i];
        ret += ' ';
    }
    ret.resize(ret.size() - 1);
    return ret;
}

//std::wstring cmdQuote(std::wstring_view str) {
//    std::wstring ret;
//    bool enquote = false;
//    for (size_t i = 0; i < str.size(); ++i) {
//        wchar_t c = str[i];
//        switch (c) {
//        break; case L' ': 
//            enquote = true; 
//        break; case L'\\': {
//            size_t after = str.find_first_not_of(L'\\', i);
//            if (after != str.npos && str[after] == L'"') {
//                ret.append((after - i) * 2 + 1, L'\\');
//                ret += L'"';
//                i = after;
//                continue;
//            }
//        }   
//        break; case L'"':
//            ret += L'"';
//        }
//        ret += c;
//    }
//    if (enquote) {
//        ret.insert(0, 1, L'"');
//        ret += L'"';
//    }
//    return ret;
//}

std::wstring myname() {
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

constexpr auto g_myGuid = L"BAF0674F-E091-468A-AAA9-234909F4CFFB";

std::wstring makePipeName(DWORD procId) {
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
                wait_time = std::min(ULONGLONG(wait_time), *m_duration - elapsed);
            }
            auto res = WaitForSingleObject(h, wait_time);
            if (res == WAIT_TIMEOUT)
                continue;
            if (res == WAIT_OBJECT_0)
                return true;
            return false;
        }
    }

    std::string formatRemaining() const {
        if (!m_duration)
            return "Infinite";
        auto elapsed = GetTickCount64() - m_start;
        ULONGLONG remaining = (elapsed <= *m_duration ? *m_duration - elapsed : 0);
        return formatDuration(remaining);
    }
private:
    std::optional<ULONGLONG> m_duration;
    ULONGLONG m_start;
};


void writeInfo(HANDLE hPipe, const WaitTracker & tracker) {
    std::string reply = tracker.formatRemaining();
    for(DWORD total = 0; total < reply.size(); ) {
        DWORD written;
        if (!WriteFile(hPipe, reply.data() + total, reply.size() - total, &written, nullptr))
            break;
        total += written;
    }
    FlushFileBuffers(hPipe);
}

void runDirect(const wchar_t * arg) {

    //Sleep(15000);
    

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);

    
    auto print = [hOut](std::wstring_view message) {
        if (hOut != 0 && hOut != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(hOut, message.data(), message.size() * sizeof(wchar_t), &written, nullptr);
        }
    };
    auto printerr = [hErr](std::wstring_view message) {
        if (hErr != 0 && hErr != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(hErr, message.data(), message.size() * sizeof(wchar_t), &written, nullptr);
        }
    };

    AutoFile hPipe =  CreateNamedPipeW(makePipeName(GetCurrentProcessId()).c_str(),
                                       PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED, 
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                       PIPE_UNLIMITED_INSTANCES, 4096, 0, 
                                       NMPWAIT_USE_DEFAULT_WAIT, nullptr);
    if (!hPipe) {
        printerr(L"child process: error creating pipe: " + widen(std::error_code(int(GetLastError()), std::system_category()).message()));
    }
    
    try {

        AutoHandle hEvent = CreateEvent(nullptr, true, true, nullptr);
        if (!hEvent)
            throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));

        std::optional<ULONGLONG> duration;
        if (arg) {
            ULONGLONG seconds = parseIntegral<ULONGLONG>(arg).value();
            if (std::numeric_limits<ULONGLONG>::max() / 1000 < seconds)
                throw WParser::ValidationError(format(Messages<wchar_t>::outOfRange(), arg));
            duration = seconds * 1000;
        }

        auto oldState = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
        if (oldState == 0)
            throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));

        print(duration ? 
                format(L"preventing sleep for {1} seconds or untill process {2} is killed\n", arg, GetCurrentProcessId()) : 
                format(L"preventing sleep indefinitely or untill process {1} is killed\n", GetCurrentProcessId()));

        if (hOut != 0 && hOut != INVALID_HANDLE_VALUE) {
            CloseHandle(hOut);
            hOut = INVALID_HANDLE_VALUE;
            SetStdHandle(STD_OUTPUT_HANDLE, hOut);
        }
        if (hErr != 0 && hErr != INVALID_HANDLE_VALUE) {
            CloseHandle(hErr);
            hErr = INVALID_HANDLE_VALUE;
            SetStdHandle(STD_ERROR_HANDLE, hOut);
        }

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
            if (ReadFile(hPipe.get(), command.data(), command.size(), &read, nullptr) && read == 4) {
                if (command == "info") {
                    writeInfo(hPipe.get(), tracker);
                } else if (command == "kill") {
                    break;
                }
            }
            DisconnectNamedPipe(hPipe.get());
        }
        

        SetThreadExecutionState(ES_CONTINUOUS);
        

    } catch(WParsingException & ex) {
        std::wstring message = L"child process error: ";
        message += ex.message();
        printerr(message);
    } catch (std::exception & ex) {
        printerr(L"child process error: " + widen(ex.what()));
    }
}




void runChild(std::optional<ULONGLONG> duration) {
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


    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.hStdOutput = hWrite.get();
    si.hStdError = hErrWrite.get();
    PROCESS_INFORMATION pi;

    if (!CreateProcess(exe.data(), GetCommandLine(), nullptr, nullptr, true, CREATE_DEFAULT_ERROR_MODE | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
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
        std::cout << std::string_view(buf, buf + read);
    }
}

bool execOnPipe(DWORD procId, std::string_view cmd, std::invocable<HANDLE> auto && proc) {
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
        if (!WriteFile(hPipe.get(), cmd.data(), cmd.size(), &written, nullptr) || written != cmd.size())
            return false;
        if constexpr (std::is_convertible_v<decltype(std::forward<decltype(proc)>(proc)(hPipe.get())), bool>)
            return std::forward<decltype(proc)>(proc)(hPipe.get());
        else {
            std::forward<decltype(proc)>(proc)(hPipe.get());
            return true;
        }
    }
}

std::optional<std::string> getInfo(DWORD procId) {
    std::string buf(256, '\0');
    if (!execOnPipe(procId, "info", [&buf](HANDLE hPipe) {
            DWORD read;
            if (!ReadFile(hPipe, buf.data(), buf.size(), &read, nullptr))
                return false;
            buf.resize(read);
            return true;
        }))
        return {};

    return buf;
}

bool kill(DWORD procId) {
    return execOnPipe(procId, "kill", [] (HANDLE) {});
}

void listProcesses() {
    WTS_PROCESS_INFO * pi;
    DWORD count;
    if (!WTSEnumerateProcesses(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pi, &count))
        throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));
    std::cout << "      PID  SESSION   REMAINING\n";
    for(DWORD i = 0; i < count; ++i) {
        auto & info = pi[i];
        if (info.pProcessName == L"keep-awake.exe"sv) {
            if (auto info = getInfo(pi[i].ProcessId)) {
                std::cout << std::format("{:>9}  {:>7}  {:>10}\n", pi[i].ProcessId, pi[i].SessionId, *info);
            }
        }
    }
}

int wmain(int argc, wchar_t * argv[]) {

    auto myenv = _wgetenv(g_myGuid);

    if (argc >= 1 && myenv && myenv == L"ON"sv) {
        runDirect(argc > 1 ? argv[1] : nullptr);
        return EXIT_SUCCESS;
    }

    std::optional<ULONGLONG> duration;
    std::optional<std::wstring> command;
    std::vector<DWORD> pidsToKill;

    WParser parser;
    try {
        parser.add(
            WOption(L"--help", L"-h"). 
            help(L"show this help message and exit"). 
            handler([&]() {  

                std::wcout << parser.formatHelp(argc ? argv[0] : L"keep-awake");
                std::exit(EXIT_SUCCESS);
        }));
        parser.add(
            WPositional(L"command").
            occurs(neverOrOnce).
            handler([&](const std::wstring_view & value) -> WExpected<void> {
                if (value == L"list" || value == L"kill") {
                    command = value;
                } else if (auto maybeVal = parseIntegral<ULONGLONG>(value)) {
                    if (std::numeric_limits<ULONGLONG>::max() / 1000 < *maybeVal)
                        return {Failure<WParser::ValidationError>, format(Messages<wchar_t>::outOfRange(), value)};
                    duration = *maybeVal;
                } else {
                    return {Failure<WParser::UnrecognizedOption>, value};
                }
                return {};
        }));
        parser.add(
            WPositional(L"args").
            occurs(zeroOrMoreTimes).
            handler([&](const std::wstring_view & value) -> WExpected<void> {

                if (command && *command == L"kill") {
                    pidsToKill.push_back(parseIntegral<DWORD>(value).value());
                    return {};
                } 
                    
                return {Failure<WParser::ExtraPositional>, value};                
        }));
        parser.addValidator([&](const WValidationData & data) {
            return !command || *command != L"kill" || !pidsToKill.empty();
        }, L"kill command requires PID arguments");
        
        auto result = parser.parse(argc, argv);
        if (auto err = result.error()) {
            std::wstring message;
            message += err->message();
            message += L"\n\n";
            message += parser.formatUsage(argc ? argv[0] : L"keep-awake");
            std::wcerr << message << L'\n';
            return EXIT_FAILURE;
        }
        

        if (command) {
            if (*command == L"list") {
                listProcesses();
                return EXIT_SUCCESS;
            } 

            assert(*command == L"kill");
            assert(!pidsToKill.empty());
            for(auto pid: pidsToKill) {
                if (kill(pid))
                    std::cout << "kill request successfully sent to process " << pid << '\n';
                else
                    std::cout << "unable to send kill request to process " << pid << '\n';
            }
            return EXIT_SUCCESS;
        }
        
        runChild(duration);

        return EXIT_SUCCESS;

    } catch (std::exception & ex) {
        std::cerr << ex.what() << '\n';
    }
    return EXIT_FAILURE;
}




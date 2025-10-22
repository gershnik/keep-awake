using namespace Argum;
using namespace std::literals;


std::wstring widen(std::string_view str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), int(str.size()), nullptr, 0);
    std::wstring ret(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), int(str.size()), ret.data(), size_needed);
    return ret;
}

std::wstring cmdQuote(std::wstring_view str) {
    std::wstring ret;
    bool enquote = false;
    for (size_t i = 0; i < str.size(); ++i) {
        wchar_t c = str[i];
        switch (c) {
        break; case L' ': 
            enquote = true; 
        break; case L'\\': {
            size_t after = str.find_first_not_of(L'\\', i);
            if (after != str.npos && str[after] == L'"') {
                ret.append((after - i) * 2 + 1, L'\\');
                ret += L'"';
                i = after;
                continue;
            }
        }   
        break; case L'"':
            ret += L'"';
        }
        ret += c;
    }
    if (enquote) {
        ret.insert(0, 1, L'"');
        ret += L'"';
    }
    return ret;
}

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

void runDirect(const wchar_t * arg) {

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hErr = GetStdHandle(STD_OUTPUT_HANDLE);
    
    try {
        ULONG duration = INFINITE;
        if (arg) {
            duration = parseIntegral<ULONG>(arg);
            if (std::numeric_limits<ULONG>::max() / 1000 < duration)
                throw WParser::ValidationError(format(Messages<wchar_t>::outOfRange(), duration));
            duration *= 1000;
        }

        auto oldState = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
        if (oldState == 0) {
            throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));
        }

        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != 0 && hOut != INVALID_HANDLE_VALUE) {
            std::wstring message = duration != INFINITE ? 
                format(L"preventing sleep for {1} seconds or untill process {2} is killed\n", arg, GetCurrentProcessId()) : 
                format(L"preventing sleep indefinitely or untill process {1} is killed\n", GetCurrentProcessId());
            DWORD written;
            WriteFile(hOut, message.c_str(), message.size() * sizeof(wchar_t), &written, nullptr);
            CloseHandle(hOut);
            hOut = INVALID_HANDLE_VALUE;
        }
        if (hErr != 0 && hErr != INVALID_HANDLE_VALUE) {
            CloseHandle(hErr);
            hErr = INVALID_HANDLE_VALUE;
        }

        Sleep(duration);
        SetThreadExecutionState(ES_CONTINUOUS);

    } catch(WParsingException & ex) {
        
        if (hErr != 0 && hErr != INVALID_HANDLE_VALUE) {
            std::wstring message = L"child process: ";
            message += ex.message();
            DWORD written;
            WriteFile(hErr, message.c_str(), message.size(), &written, nullptr);
        }

    } catch (std::exception & ex) {
        if (hErr != 0 && hErr != INVALID_HANDLE_VALUE) {
            std::wstring message = L"child process: " + widen(ex.what());
            DWORD written;
            WriteFile(hErr, message.c_str(), message.size(), &written, nullptr);
        }
    }
}

int wmain(int argc, wchar_t * argv[]) {

    if (argc >= 2 && argv[1] == L"--exec"sv) {
        runDirect(argc > 2 ? argv[2] : nullptr);
        return EXIT_SUCCESS;
    }

    std::optional<ULONG> duration;

    WParser parser;
    try {
        parser.add(
            WPositional(L"duration").
            help(L"how long to keep the machine awake (in seconds)").
            occurs(neverOrOnce).
            handler([&](const std::wstring_view & value) { 
                ULONG val = parseIntegral<ULONG>(value);
                if (std::numeric_limits<ULONG>::max() / 1000 < val)
                    throw WParser::ValidationError(Argum::format(Messages<wchar_t>::outOfRange(), value));
                duration = val;
        }));

        parser.parse(argc, argv);
    
        std::wstring cmd = cmdQuote(myname());
        cmd += L" --exec";
        if (duration) {
            cmd += L' ';
            cmd += std::to_wstring(*duration);
        }

        SECURITY_ATTRIBUTES pipeAttr;
        pipeAttr.nLength = sizeof(pipeAttr); 
        pipeAttr.bInheritHandle = true; 
        pipeAttr.lpSecurityDescriptor = nullptr; 

        HANDLE hRead, hWrite;
        if (!CreatePipe(&hRead, &hWrite, &pipeAttr, 0))
            throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));
        if (!SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0))
            throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));


        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = INVALID_HANDLE_VALUE;
        si.hStdOutput = hWrite;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION pi;

        if (!CreateProcess(nullptr, &cmd[0], nullptr, nullptr, true, CREATE_DEFAULT_ERROR_MODE | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hRead);
            CloseHandle(hWrite);
            throw std::system_error(std::error_code(int(GetLastError()), std::system_category()));
        }

        CloseHandle(hWrite);
        while (true) {
            char buf[4096];
            DWORD read;
            if (!ReadFile(hRead, buf, sizeof(buf), &read, nullptr))
                break;
            std::cout << std::string_view(buf, buf + read);
        }
        CloseHandle(hRead);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        return EXIT_SUCCESS;

    } catch (WParsingException & ex) {
        std::wstring message;
        message += ex.message();
        message += L"\n\n";
        message += parser.formatUsage(argc ? argv[0] : L"keep-awake");
        std::wcerr << message << L'\n';
    } catch (std::exception & ex) {
        std::cerr << ex.what() << '\n';
    }
    return EXIT_FAILURE;
}




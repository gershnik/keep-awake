using namespace Argum;

int APIENTRY wWinMain([[maybe_unused]] _In_ HINSTANCE hInstance,
                      [[maybe_unused]] _In_opt_ HINSTANCE hPrevInstance,
                      _In_ LPWSTR    lpCmdLine,
                      [[maybe_unused]] _In_ int nCmdShow) {

    HANDLE eventLog = RegisterEventSource(nullptr, L"keep-awake");

    
    ULONG duration = INFINITE;

    WParser parser;
    parser.add(
        WPositional(L"duration").
        help(L"how long to keep the machine awake (in seconds)").
        occurs(neverOrOnce).
        handler([&](const std::wstring_view & value) { 
            duration = parseIntegral<ULONG>(value);
            if (std::numeric_limits<ULONG>::max() / 1000 < duration)
                throw WParser::ValidationError(L"duration value is too large");
            duration *= 1000;
    }));

    //the argument is bogus, we need the real thing
    lpCmdLine = GetCommandLineW();
    auto deleteArgv = [](wchar_t** ptr) {LocalFree(ptr); };
    int argc;
    std::unique_ptr<wchar_t *, decltype(deleteArgv)> argv(CommandLineToArgvW(lpCmdLine, &argc), deleteArgv);
    try {
        parser.parse(argc, argv.get());
    } catch (WParsingException & ex) {
        std::wstring message;
        message += ex.message();
        message += L'\n';
        message += parser.formatUsage(argc ? argv.get()[0] : L"keep-awake");

        auto mptr = message.c_str();
        ReportEvent(eventLog, EVENTLOG_ERROR_TYPE, 0, ERROR_BAD_ARGUMENTS, nullptr, 1, 0, &mptr, nullptr);
        return EXIT_FAILURE;
    }
    
    
    auto oldState = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
    if (oldState == 0) {
        auto message = L"Keep Awake Failed";
        ReportEvent(eventLog, EVENTLOG_ERROR_TYPE, 0, ERROR_INVALID_FUNCTION, nullptr, 1, 0, &message, nullptr);
        return 1;
    }
    Sleep(duration);
    SetThreadExecutionState(ES_CONTINUOUS);
    return 0;
}




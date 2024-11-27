// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Andrey Semin and many others

#include <array>
#include <iostream>
#include <cassert>
#include <climits>
#include <algorithm>
#ifdef _MSC_VER
#include <windows.h>
#include <accctrl.h>
#include <aclapi.h>
#include <sddl.h>
#include <process.h>
#include <comdef.h>
#else
#include <sys/wait.h> // for waitpid()
#include <unistd.h> // for ::sleep
#endif
#include "utils.h"
#include "cpucounters.h"
#include <numeric>
#ifndef _MSC_VER
#include <execinfo.h>
extern char ** environ;
#endif
#ifdef __linux__
#include <glob.h>
#endif

namespace pcm {


bool startsWithPCM(const StringType& varName) {
    const StringType prefix = PCM_STRING("PCM_");
    return varName.compare(0, prefix.size(), prefix) == 0;
}

bool isInKeepList(const StringType& varName, const std::vector<StringType>& keepList) {
    for (const auto& keepVar : keepList) {
        if (varName == keepVar) {
            return true;
        }
    }
    return false;
}

#if defined(_MSC_VER)

void eraseEnvironmentVariables(const std::vector<std::wstring>& keepList) {
    // Get a snapshot of the current environment block
    LPWCH envBlock = GetEnvironmentStrings();
    if (!envBlock) {
        std::cerr << "Error getting environment strings." << std::endl;
        return;
    }

    // Iterate over the environment block
    for (LPWCH var = envBlock; *var != 0; var += std::wcslen(var) + 1) {
        std::wstring varName(var);
        size_t pos = varName.find('=');
        if (pos != std::string::npos) {
            varName = varName.substr(0, pos);
            if (!startsWithPCM(varName) && !isInKeepList(varName, keepList)) {
                SetEnvironmentVariable(varName.c_str(), NULL);
            }
        }
    }

    // Free the environment block
    FreeEnvironmentStrings(envBlock);
}
#else
void eraseEnvironmentVariables(const std::vector<std::string>& keepList) {
    std::vector<std::string> varsToDelete;

    // Collect all the variables that need to be deleted
    for (char **env = environ; *env != nullptr; ++env) {
        std::string envEntry(*env);
        size_t pos = envEntry.find('=');
        if (pos != std::string::npos) {
            std::string varName = envEntry.substr(0, pos);
            if (!startsWithPCM(varName) && !isInKeepList(varName, keepList)) {
                varsToDelete.push_back(varName);
            }
        }
    }

    // Delete the collected variables
    for (const auto& varName : varsToDelete) {
        unsetenv(varName.c_str());
    }
}
#endif

void (*post_cleanup_callback)(void) = NULL;

//! \brief handler of exit() call
void exit_cleanup(void)
{
    std::cout << std::flush;

    restore_signal_handlers();

    // this replaces same call in cleanup() from util.h
    if (PCM::isInitialized()) PCM::getInstance()->cleanup(); // this replaces same call in cleanup() from util.h

//TODO: delete other shared objects.... if any.

    if(post_cleanup_callback != NULL)
    {
        post_cleanup_callback();
    }
}

bool colorEnabled = false;

void setColorEnabled(bool value)
{
    colorEnabled = value;
}

const char * setColor (const char * colorStr)
{
    return colorEnabled ? colorStr : "";
}

template<typename T, typename... N>
constexpr auto make_array(N&&... args) -> std::array<T, sizeof...(args)>
{
    return {std::forward<N>(args)...};
}

constexpr auto colorTable{make_array<const char*>(
    ASCII_GREEN,
    ASCII_YELLOW,
    ASCII_MAGENTA,
    ASCII_CYAN,
    ASCII_BRIGHT_GREEN,
    ASCII_BRIGHT_YELLOW,
    ASCII_BRIGHT_BLUE,
    ASCII_BRIGHT_MAGENTA,
    ASCII_BRIGHT_CYAN,
    ASCII_BRIGHT_WHITE
)};

size_t currentColor = 0;
const char * setNextColor()
{
    const auto result = setColor(colorTable[currentColor++]);
    if (currentColor == colorTable.size())
    {
        currentColor = 0;
    }
    return result;
}

const char * resetColor()
{
    currentColor = 0;
    return setColor(ASCII_RESET_COLOR);
}

void print_cpu_details()
{
    const auto m = PCM::getInstance();
    std::cerr << "\nDetected " << m->getCPUBrandString() << " \"Intel(r) microarchitecture codename " <<
        m->getUArchCodename() << "\" stepping " << m->getCPUStepping();
    const auto ucode_level = m->getCPUMicrocodeLevel();
    if (ucode_level >= 0)
    {
        std::cerr << " microcode level 0x" << std::hex << ucode_level << std::dec;
    }
    std::cerr << "\n";
}

#ifdef __linux__
std::vector<std::string> findPathsFromPattern(const char* pattern)
{
            std::vector<std::string> result;
            glob_t glob_result;
            memset(&glob_result, 0, sizeof(glob_result));
            if (glob(pattern, GLOB_TILDE, nullptr, &glob_result) == 0)
            {
                for (size_t i = 0; i < glob_result.gl_pathc; ++i)
                {
                    result.push_back(glob_result.gl_pathv[i]);
                }
            }
            globfree(&glob_result);
            return result;
};
#endif

#ifdef _MSC_VER

ThreadGroupTempAffinity::ThreadGroupTempAffinity(uint32 core_id, bool checkStatus, const bool restore_)
  : restore(restore_)
{
    GROUP_AFFINITY NewGroupAffinity;
    SecureZeroMemory(&NewGroupAffinity, sizeof(GROUP_AFFINITY));
    SecureZeroMemory(&PreviousGroupAffinity, sizeof(GROUP_AFFINITY));
    DWORD currentGroupSize = 0;

    while ((DWORD)core_id >= (currentGroupSize = GetActiveProcessorCount(NewGroupAffinity.Group)))
    {
        if (currentGroupSize == 0)
        {
            std::cerr << "ERROR: GetActiveProcessorCount for core " << core_id << " failed with error " << GetLastError() << "\n";
            throw std::exception();
        }
        core_id -= (uint32)currentGroupSize;
        ++NewGroupAffinity.Group;
    }
    NewGroupAffinity.Mask = 1ULL << core_id;
    if (GetThreadGroupAffinity(GetCurrentThread(), &PreviousGroupAffinity)
        && (std::memcmp(&NewGroupAffinity, &PreviousGroupAffinity, sizeof(GROUP_AFFINITY)) == 0))
    {
        restore = false;
        return;
    }
    const auto res = SetThreadGroupAffinity(GetCurrentThread(), &NewGroupAffinity, &PreviousGroupAffinity);
    if (res == FALSE && checkStatus)
    {
        std::cerr << "ERROR: SetThreadGroupAffinity for core " << core_id << " failed with error " << GetLastError() << "\n";
        throw std::exception();
    }
}
ThreadGroupTempAffinity::~ThreadGroupTempAffinity()
{
    if (restore) SetThreadGroupAffinity(GetCurrentThread(), &PreviousGroupAffinity, NULL);
}

LONG unhandled_exception_handler(LPEXCEPTION_POINTERS p)
{
    std::cerr << "DEBUG: Unhandled Exception event\n";
    exit(EXIT_FAILURE);
}

/**
* \brief version of interrupt handled for Windows
*/
BOOL sigINT_handler(DWORD fdwCtrlType)
{
    // output for DEBUG only
    std::cerr << "DEBUG: caught signal to interrupt: ";
    switch (fdwCtrlType)
    {
    // Handle the CTRL-C signal.
    case CTRL_C_EVENT:
        std::cerr << "Ctrl-C event\n";
        break;

    // CTRL-CLOSE: confirm that the user wants to exit.
    case CTRL_CLOSE_EVENT:
        std::cerr << "Ctrl-Close event\n";
        break;

    // Pass other signals to the next handler.
    case CTRL_BREAK_EVENT:
        std::cerr << "Ctrl-Break event\n";
        break;

    case CTRL_LOGOFF_EVENT:
        std::cerr << "Ctrl-Logoff event\n";
        break;

    case CTRL_SHUTDOWN_EVENT:
        std::cerr << "Ctrl-Shutdown event\n";
        break;

    default:
        std::cerr << "Unknown event\n";
        break;
    }

    // TODO: dump summary, if needed

    // in case PCM is blocked just return and summary will be dumped in
    // calling function, if needed
    if (PCM::isInitialized() && PCM::getInstance()->isBlocked()) {
        return FALSE;
    } else {
        exit_cleanup();
        _exit(EXIT_SUCCESS);
        return FALSE; // to prevent Warning
    }
}

/**
* \brief started in a separate thread and blocks waiting for child application to exit.
* After child app exits: -> print Child's termination status and terminates PCM
*/
void waitForChild(void * proc_id)
{
    intptr_t procHandle = (intptr_t)proc_id;
    int termstat;
    _cwait(&termstat, procHandle, _WAIT_CHILD);
    std::cerr << "Program exited with status " << termstat << "\n";
    exit(EXIT_SUCCESS);
}
#else
/**
 * \brief handles signals that lead to termination of the program
 * such as SIGINT, SIGQUIT, SIGABRT, SIGSEGV, SIGTERM, SIGCHLD
 * this function specifically works when the client application launched
 * by pcm -- terminates
 */
void sigINT_handler(int signum)
{
    // output for DEBUG only
    std::cerr << "DEBUG: caught signal to interrupt (" << strsignal(signum) << ").\n";
    // TODO: dump summary, if needed

    // in case PCM is blocked just return and summary will be dumped in
    // calling function, if needed
    if (PCM::isInitialized() && PCM::getInstance()->isBlocked()) {
        return;
    } else {
        exit_cleanup();
        if (signum == SIGABRT || signum == SIGSEGV)
        {
            _exit(EXIT_FAILURE);
        }
        else
        {
            _exit(EXIT_SUCCESS);
        }
    }
}

constexpr auto BACKTRACE_MAX_STACK_FRAME = 30;
void printBacktrace()
{
    void* backtrace_buffer[BACKTRACE_MAX_STACK_FRAME] = { 0 };
    char** backtrace_strings = NULL;
    size_t backtrace_size = 0;

    backtrace_size = backtrace(backtrace_buffer, BACKTRACE_MAX_STACK_FRAME);
    backtrace_strings = backtrace_symbols(backtrace_buffer, backtrace_size);
    if (backtrace_strings == NULL)
    {
        std::cerr << "Debug: backtrace empty. \n";
    }
    else
    {
        std::cerr << "Debug: backtrace dump(" << backtrace_size << " stack frames).\n";
        for (size_t i = 0; i < backtrace_size; i++)
        {
            std::cerr << backtrace_strings[i] << "\n";
        }
        freeAndNullify(backtrace_strings);
    }
}

/**
 * \brief handles SIGSEGV signals that lead to termination of the program
 * this function specifically works when the client application launched
 * by pcm -- terminates
 */
void sigSEGV_handler(int signum)
{
    printBacktrace();
    sigINT_handler(signum);
}

/**
 * \brief handles signals that lead to restart the application
 * such as SIGHUP.
 * for example to re-read environment variables controlling PCM execution
 */
void sigHUP_handler(int /*signum*/)
{
    // output for DEBUG only
    std::cerr << "DEBUG: caught signal to hangup. Reloading configuration and continue...\n";
    // TODO: restart; so far do nothing

    return; // continue program execution
}

/**
 * \brief handles signals that lead to update of configuration
 * such as SIGUSR1 and SIGUSR2.
 * for the future extensions
 */
void sigUSR_handler(int /*signum*/)
{
    std::cerr << "DEBUG: caught USR signal. Continue.\n";
    // TODO: reload configurationa, reset accumulative counters;

    return;
}

/**
 * \brief handles signals that lead to update of configuration
 * such as SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU
 */
void sigSTOP_handler(int /*signum*/)
{
    PCM * m = PCM::getInstance();
    int runState = m->getRunState();
    std::string state = (runState == 1 ? "suspend" : "continue");
    std::cerr << "DEBUG: caught signal to " << state << " execution.\n"; // debug of signals only
    if (runState == 1) {
        // stop counters and sleep... almost forever;
        m->setRunState(0);
        sleep(INT_MAX);
    } else {
        // resume
        m->setRunState(1);
        alarm(1);
    }
    return;
}

/**
* \brief handles signals that lead to update of configuration
* such as SIGCONT
*/
void sigCONT_handler(int /*signum*/)
{
    std::cout << "DEBUG: caught signal to continue execution.\n"; // debug of signals only
    // TODO: clear counters, resume counting.
    return;
}
#endif // ifdef _MSC_VER

//! \brief install various handlers for system signals
void set_signal_handlers(void)
{
    if (atexit(exit_cleanup) != 0)
    {
        std::cerr << "ERROR: Failed to install exit handler.\n";
        return;
    }

#ifdef _MSC_VER
    BOOL handlerStatus;
    // Increase the priority a bit to improve context switching delays on Windows
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
// to fix Cygwin/BASH setting Ctrl+C handler need first to restore the default one
    handlerStatus = SetConsoleCtrlHandler(NULL, FALSE); // restores normal processing of CTRL+C input
    if (handlerStatus == 0) {
        tcerr << "Failed to set Ctrl+C handler. Error code: " << GetLastError() << " ";
        const TCHAR * errorStr = _com_error(GetLastError()).ErrorMessage();
        if (errorStr) tcerr << errorStr;
        tcerr << "\n";
        _exit(EXIT_FAILURE);
    }
    handlerStatus = SetConsoleCtrlHandler((PHANDLER_ROUTINE)sigINT_handler, TRUE);
    if (handlerStatus == 0) {
        tcerr << "Failed to set Ctrl+C handler. Error code: " << GetLastError() << " ";
        const TCHAR * errorStr = _com_error(GetLastError()).ErrorMessage();
        if (errorStr) tcerr << errorStr;
        tcerr << "\n";
        _exit(EXIT_FAILURE);
    }
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)&unhandled_exception_handler);
    char *envPath;
    if (_dupenv_s(&envPath, NULL, "_"))
    {
        std::cerr << "\nPCM ERROR: _dupenv_s failed.\n";
        _exit(EXIT_FAILURE);
    }
    if (envPath)
    {
        std::cerr << "\nPCM ERROR: Detected cygwin/mingw environment which does not allow to setup PMU clean-up handlers on Ctrl-C and other termination signals.\n";
        std::cerr << "See https://www.mail-archive.com/cygwin@cygwin.com/msg74817.html\n";
        std::cerr << "As a workaround please run pcm directly from a native windows shell (e.g. cmd).\n";
        std::cerr << "Exiting...\n\n";
        freeAndNullify(envPath);
        _exit(EXIT_FAILURE);
    }
    freeAndNullify(envPath);
    std::cerr << "DEBUG: Setting Ctrl+C done.\n";

#else
    struct sigaction saINT, saHUP, saUSR, saSTOP, saCONT;

    // install handlers that interrupt execution
    saINT.sa_handler = sigINT_handler;
    sigemptyset(&saINT.sa_mask);
    saINT.sa_flags = SA_RESTART;
    sigaction(SIGINT, &saINT, NULL);
    sigaction(SIGQUIT, &saINT, NULL);
    sigaction(SIGABRT, &saINT, NULL);
    sigaction(SIGTERM, &saINT, NULL);
    
    saINT.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &saINT, NULL); // get there is our child exits. do nothing if it stopped/continued

    saINT.sa_handler = sigSEGV_handler;
    sigemptyset(&saINT.sa_mask);
    saINT.sa_flags = SA_RESTART;
    sigaction(SIGSEGV, &saINT, NULL);

    // install SIGHUP handler to restart
    saHUP.sa_handler = sigHUP_handler;
    sigemptyset(&saHUP.sa_mask);
    saHUP.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &saHUP, NULL);

    // install SIGHUP handler to restart
    saUSR.sa_handler = sigUSR_handler;
    sigemptyset(&saUSR.sa_mask);
    saUSR.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &saUSR, NULL);
    sigaction(SIGUSR2, &saUSR, NULL);

    // install SIGSTOP handler: pause/resume
    saSTOP.sa_handler = sigSTOP_handler;
    sigemptyset(&saSTOP.sa_mask);
    saSTOP.sa_flags = SA_RESTART;
    sigaction(SIGSTOP, &saSTOP, NULL);
    sigaction(SIGTSTP, &saSTOP, NULL);
    sigaction(SIGTTIN, &saSTOP, NULL);
    sigaction(SIGTTOU, &saSTOP, NULL);

    // install SIGCONT & SIGALRM handler
    saCONT.sa_handler = sigCONT_handler;
    sigemptyset(&saCONT.sa_mask);
    saCONT.sa_flags = SA_RESTART;
    sigaction(SIGCONT, &saCONT, NULL);
    sigaction(SIGALRM, &saCONT, NULL);

#endif
    return;
}

//! \brief Restores default signal handlers under Linux/UNIX
void restore_signal_handlers(void)
{
#ifndef _MSC_VER
    struct sigaction action;
    action.sa_handler = SIG_DFL;
    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGQUIT, &action, NULL);
    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGSEGV, &action, NULL);

    sigaction(SIGCHLD, &action, NULL);

    // restore SIGHUP handler to restart
    sigaction(SIGHUP, &action, NULL);

    // restore SIGHUP handler to restart
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGUSR2, &action, NULL);

    // restore SIGSTOP handler: pause/resume
//     sigaction(SIGSTOP, &action, NULL); // cannot catch this
// handle SUSP character: normally C-z)
    sigaction(SIGTSTP, &action, NULL);
    sigaction(SIGTTIN, &action, NULL);
    sigaction(SIGTTOU, &action, NULL);

    // restore SIGCONT & SIGALRM handler
    sigaction(SIGCONT, &action, NULL);
    sigaction(SIGALRM, &action, NULL);
#endif

    return;
}

void set_real_time_priority(const bool & silent)
{
    if (!silent)
    {
        std::cerr << "Setting real time priority for the process\n";
    }
#ifdef _MSC_VER
    if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS))
    {
        std::cerr << "ERROR: SetPriorityClass with REALTIME_PRIORITY_CLASS failed with error " << GetLastError() << "\n";
    }
    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
    {
        std::cerr << "ERROR: SetThreadPriority with THREAD_PRIORITY_TIME_CRITICAL failed with error " << GetLastError() << "\n";
    }
#elif __linux__
    const auto priority = sched_get_priority_max(SCHED_RR);
    if (priority == -1)
    {
        std::cerr << "ERROR: Could not get SCHED_RR max priority: " << strerror(errno) << "\n";
    }
    else
    {
        struct sched_param sp = { .sched_priority = priority };
        if (sched_setscheduler(0, SCHED_RR, &sp) == -1)
        {
            const auto errnosave = errno;
            std::cerr << "ERROR: Could not set scheduler to realtime! Errno: " << errnosave << " Error message: \"" << strerror(errnosave) << "\"\n";
        }
        else
        {
            if (!silent)
            {
                std::cerr << "Scheduler changed to SCHED_RR and priority to " << priority << "\n";
            }
        }
    }
#else
    std::cerr << "Setting real time priority for the process not implemented on your OS.\n";
#endif
}

void set_post_cleanup_callback(void(*cb)(void))
{
    post_cleanup_callback = cb;
}

//!\brief launches external program in a separate process
void MySystem(char * sysCmd, char ** sysArgv)
{
    if (sysCmd == NULL) {
        assert("No program provided. NULL pointer");
        exit(EXIT_FAILURE);
    }
    std::cerr << "\nExecuting \"";
    std::cerr << sysCmd;
    std::cerr << "\" command:\n";
#ifdef _MSC_VER
    intptr_t ret;
    char cbuf[128];

    if (PCM::getInstance()->isBlocked()) {  // synchronous start: wait for child process completion
        // in case PCM should be blocked waiting for the child application to end
        // 1. returns and ret = -1 in case of error creating process is encountered
        // 2.
        ret = _spawnvp(_P_WAIT, sysCmd, sysArgv);
        if (ret == -1) { // process creation failed.
            strerror_s(cbuf, 128, errno);
            std::cerr << "Failed to start program \"" << sysCmd << "\". " << cbuf << "\n";
            exit(EXIT_FAILURE);
        } else {         // process created, worked, and completed with exist code in ret. ret=0 -> Success
            std::cerr << "Program exited with status " << ret << "\n";
        }
    } else {             // async start: PCM works in parallel with the child process, and exits when
        ret = _spawnvp(_P_NOWAIT, sysCmd, sysArgv);
        if (ret == -1) {
            strerror_s(cbuf, 128, errno);
            std::cerr << "Failed to start program \"" << sysCmd << "\". " << cbuf << "\n";
            exit(EXIT_FAILURE);
        } else { // ret here is the new process handle.
            // start new thread which will wait for child completion, and continue PCM's execution
            if (_beginthread(waitForChild, 0, (void *)ret) == -1L) {
                strerror_s(cbuf, 128, errno);
                std::cerr << "WARNING: Failed to set waitForChild. PCM will continue infinitely: finish it manually! " << cbuf << "\n";
            }
        }
    }
#else
    pid_t child_pid = fork();

    if (child_pid == 0) {
        execvp(sysCmd, sysArgv);
        std::cerr << "Failed to start program \"" << sysCmd << "\"\n";
        exit(EXIT_FAILURE);
    }
    else
    {
        if (PCM::getInstance()->isBlocked()) {
            int res;
            waitpid(child_pid, &res, 0);
            std::cerr << "Program " << sysCmd << " launched with PID: " << std::dec << child_pid << "\n";

            if (WIFEXITED(res)) {
                std::cerr << "Program exited with status " << WEXITSTATUS(res) << "\n";
            }
            else if (WIFSIGNALED(res)) {
                std::cerr << "Process " << child_pid << " was terminated with status " << WTERMSIG(res) << "\n";
            }
        }
    }
#endif
}

#ifdef _MSC_VER
#define HORIZONTAL     char(196)
#define VERTICAL       char(179)
#define DOWN_AND_RIGHT char(218)
#define DOWN_AND_LEFT  char(191)
#define UP_AND_RIGHT   char(192)
#define UP_AND_LEFT    char(217)
#else
#define HORIZONTAL     u8"\u2500"
#define VERTICAL       u8"\u2502"
#define DOWN_AND_RIGHT u8"\u250C"
#define DOWN_AND_LEFT  u8"\u2510"
#define UP_AND_RIGHT   u8"\u2514"
#define UP_AND_LEFT    u8"\u2518"
#endif

template <class T>
void drawBar(const int nempty, const T & first, const int width, const T & last)
{
    for (int c = 0; c < nempty; ++c)
    {
        std::cout << ' ';
    }
    std::cout << first;
    for (int c = 0; c < width; ++c)
    {
        std::cout << HORIZONTAL;
    }
    std::cout << last << '\n';
}

void drawStackedBar(const std::string & label, std::vector<StackedBarItem> & h, const int width)
{
    int real_width = 0;
    auto scale = [&width](double fraction)
    {
        return int(round(fraction * double(width)));
    };
    for (const auto & i : h)
    {
        real_width += scale(i.fraction);
    }
    if (real_width > 2*width)
    {
        std::cout << "ERROR: sum of fractions > 2 ("<< real_width << " > " << width << ")\n";
        return;
    }
    drawBar((int)label.length(), DOWN_AND_RIGHT, real_width, DOWN_AND_LEFT);
    std::cout << label << VERTICAL;
    for (const auto & i : h)
    {
        const int c_width = scale(i.fraction);
        for (int c = 0; c < c_width; ++c)
        {
            std::cout << i.fill;
        }
    }
    std::cout << VERTICAL << "\n";
    drawBar((int)label.length(), UP_AND_RIGHT, real_width, UP_AND_LEFT);
}


bool CheckAndForceRTMAbortMode(const char * arg, PCM * m)
{
    if (check_argument_equals(arg, {"-force-rtm-abort-mode"}))
    {
        if (nullptr == m)
        {
            m = PCM::getInstance();
            assert(m);
        }
        m->enableForceRTMAbortMode();
        return true;
    }
    return false;
}

std::vector<std::string> split(const std::string & str, const char delim)
{
    std::string token;
    std::vector<std::string> result;
    std::istringstream strstr(str);
    while (std::getline(strstr, token, delim))
    {
        result.push_back(token);
    }
    return result;
}

uint64 read_number(const char* str)
{
    std::istringstream stream(str);
    if (strstr(str, "x")) stream >> std::hex;
    uint64 result = 0;
    stream >> result;
    return result;
}

// emulates scanf %i for hex 0x prefix otherwise assumes dec (no oct support)
bool match(const std::string& subtoken, const std::string& sname, uint64* result)
{
    if (pcm_sscanf(subtoken) >> s_expect(sname + "0x") >> std::hex >> *result)
        return true;

    if (pcm_sscanf(subtoken) >> s_expect(sname) >> std::dec >> *result)
        return true;

    return false;
}

#define PCM_CALIBRATION_INTERVAL 50 // calibrate clock only every 50th iteration

int calibratedSleep(const double delay, const char* sysCmd, const MainLoop& mainLoop, PCM* m)
{
    static uint64 TimeAfterSleep = 0;
    int delay_ms = int(delay * 1000);

    if (TimeAfterSleep) delay_ms -= (int)(m->getTickCount() - TimeAfterSleep);
    if (delay_ms < 0) delay_ms = 0;

    if (sysCmd == NULL || mainLoop.getNumberOfIterations() != 0 || m->isBlocked() == false)
    {
        if (delay_ms > 0)
        {
            // std::cerr << "DEBUG: sleeping for " << std::dec << delay_ms << " ms...\n";
            MySleepMs(delay_ms);
        }
    }

    TimeAfterSleep = m->getTickCount();

    return delay_ms;
};

void print_help_force_rtm_abort_mode(const int alignment, const char * separator)
{
    if (PCM::isForceRTMAbortModeAvailable() == false)
    {
        return;
    }
    try
    {
        const auto m = PCM::getInstance();
        if (m->getMaxCustomCoreEvents() < 4)
        {
            std::cout << "  -force-rtm-abort-mode";
            for (int i = 0; i < (alignment - 23); ++i)
            {
                std::cout << " ";
            }
            assert(separator);
            std::cout << separator << " force RTM transaction abort mode to enable more programmable counters\n";
        }
    }
    catch (std::exception & e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
    }
    catch (...)
    {
        std::cerr << "ERROR: Unknown exception caught in print_help_force_rtm_abort_mode\n";
    }
}

#ifdef _MSC_VER
std::string safe_getenv(const char* env)
{
    char * buffer;
    std::string result;
    if (_dupenv_s(&buffer, NULL, env) == 0 && buffer != nullptr)
    {
        result = buffer;
        freeAndNullify(buffer);
    }
    return result;
}
#else
std::string safe_getenv(const char* env)
{
    const auto getenvResult = std::getenv(env);
    return getenvResult ? std::string(getenvResult) : std::string("");
}
#endif

void print_pid_collection_message(int pid)
{
    if (pid != -1)
    {
        std::cerr << "Collecting core metrics for process ID " << std::dec << pid << "\n";
    }
}

double parse_delay(const char *arg, const std::string& progname, print_usage_func print_usage_func)
{
    // any other options positional that is a floating point number is treated as <delay>,
    // while the other options are ignored with a warning issues to stderr
    double delay_input = 0.0;
    std::istringstream is_str_stream(arg);
    is_str_stream >> std::noskipws >> delay_input;
    if(is_str_stream.eof() && !is_str_stream.fail())
    {
        if (delay_input < 0)
        {
            std::cerr << "Invalid delay specified: \"" << *arg << "\". Delay should be positive.\n";
            if(print_usage_func)
            {
                print_usage_func(progname);
            }
            exit(EXIT_FAILURE);
        }
        return delay_input;
    }
    else
    {
        std::cerr << "WARNING: unknown command-line option: \"" << *arg << "\". Ignoring it.\n";
        if(print_usage_func)
        {
            print_usage_func(progname);
        }
        exit(EXIT_FAILURE);
    }
}

std::list<int> extract_integer_list(const char *optarg){
    const char *pstr = optarg;
    std::list<int> corelist;
    std::string snum1, snum2;
    std::string *pnow = &snum1;
    char nchar = ',';
    while(*pstr != '\0' || nchar != ','){
        nchar = ',';
        if (*pstr != '\0'){
            nchar = *pstr;
            pstr++;
        } 
        //printf("c=%c\n",nchar);
        if (nchar=='-' && pnow == &snum1 && snum1.size()>0){
            pnow = &snum2;
        }else if (nchar == ','){
            if (!snum1.empty() && !snum2.empty()){
                int num1 = atoi(snum1.c_str()), num2 =atoi(snum2.c_str());
                if (num2 < num1) std::swap(num1,num2);
                if (num1 < 0) num1 = 0;
                for (int ix=num1; ix <= num2; ix++){
                    corelist.push_back(ix);
                }
            }else if (!snum1.empty()){
                int num1 = atoi(snum1.c_str());
                corelist.push_back(num1);
            }
            snum1.clear();
            snum2.clear();
            pnow = &snum1;
        }else if (nchar != ' '){
            pnow->push_back(nchar);
        }
    }
    return(corelist);
}

bool extract_argument_value(const char* arg, std::initializer_list<const char*> arg_names, std::string& value)
{
    const auto arg_len = strlen(arg);
    for (const auto& arg_name: arg_names) {
        const auto arg_name_len = strlen(arg_name);
        if (arg_len > arg_name_len && strncmp(arg, arg_name, arg_name_len) == 0 && arg[arg_name_len] == '=') {
            value = arg + arg_name_len + 1;
            const auto last_pos = value.find_last_not_of("\"");
            if (last_pos != std::string::npos) {
                value.erase(last_pos + 1);
            }
            const auto first_pos = value.find_first_not_of("\"");
            if (first_pos != std::string::npos) {
                value.erase(0, first_pos);
            }

            return true;
        }
    }

    return false;
}

bool check_argument_equals(const char* arg, std::initializer_list<const char*> arg_names)
{
    const auto arg_len = strlen(arg);
    for (const auto& arg_name: arg_names) {
        if (arg_len == strlen(arg_name) && strncmp(arg, arg_name, arg_len) == 0) {
            return true;
        }
    }

    return false;
}

void check_and_set_silent(int argc, char * argv[], null_stream &nullStream2)
{
    if (argc > 1) do
    {
        argv++;
        argc--;

        if (check_argument_equals(*argv, {"--help", "-h", "/h"}) ||
            check_argument_equals(*argv, {"-silent", "/silent"}))
        {
            std::cerr.rdbuf(&nullStream2);
            return;
        }
    } while (argc > 1);
}

bool check_for_injections(const std::string & str)
{
    const std::array<char, 4> symbols = {'=', '+', '-', '@'};
    if (std::find(std::begin(symbols), std::end(symbols), str[0]) != std::end(symbols)) {
        std::cerr << "ERROR: First letter in event name: " << str << " cannot be \"" << str[0] << "\" , please use escape \"\\\" or remove it\n";
        return true;
    }
    return false;
}

void print_enforce_flush_option_help()
{
    std::cout << "  -f    | /f                         => enforce flushing output\n";
}

bool print_version(int argc, char * argv[])
{
    if (argc > 1) do
    {
        argv++;
        argc--;

        if (check_argument_equals(*argv, {"--version"}))
        {
            std::cout << "version: " << PCM_VERSION << "\n";
            return true;
        }
    } while (argc > 1);

    return false;
}

std::string dos2unix(std::string in)
{
    if (in.length() > 0 && int(in[in.length() - 1]) == 13)
    {
        in.erase(in.length() - 1);
    }
    return in;
}

bool isRegisterEvent(const std::string & pmu)
{
    if (pmu == "mmio"
       || pmu == "pcicfg"
       || pmu == "pmt"
       || pmu == "package_msr"
       || pmu == "thread_msr")
    {
        return true;
    }
    return false;
}

std::string a_title(const std::string &init, const std::string &name) {
    char begin = init[0];
    std::string row = init;
    row += name;
    return row + begin;
}

std::string a_data (std::string init, struct data d) {
    char begin = init[0];
    std::string row = init;
    std::string str_d = unit_format(d.value);
    row += str_d;
    if (str_d.size() > d.width)
        throw std::length_error("counter value > event_name length");
    row += std::string(d.width - str_d.size(), ' ');
    return row + begin;
}

std::string build_line(std::string init, std::string name, bool last_char = true, char this_char = '_')
{
    char begin = init[0];
    std::string row = init;
    row += std::string(name.size(), this_char);
    if (last_char == true)
        row += begin;
    return row;
}

std::string a_header_footer (std::string init, std::string name)
{
    return build_line(init, name);
}

std::string build_csv_row(const std::vector<std::string>& chunks, const std::string& delimiter)
{
    return std::accumulate(chunks.begin(), chunks.end(), std::string(""),
                           [delimiter](const std::string &left, const std::string &right){
                               return left.empty() ? right : left + delimiter + right;
                           });
}


std::vector<struct data> prepare_data(const std::vector<uint64_t> &values, const std::vector<std::string> &headers)
{
    std::vector<struct data> v;
    uint32_t idx = 0;
    for (std::vector<std::string>::const_iterator iunit = std::next(headers.begin()); iunit != headers.end() && idx < values.size(); ++iunit, idx++)
    {
        struct data d;
        d.width = (uint32_t)iunit->size();
        d.value = values[idx];
        v.push_back(d);
    }

    return v;
}

void display(const std::vector<std::string> &buff, std::ostream& stream)
{
    for (std::vector<std::string>::const_iterator iunit = buff.begin(); iunit != buff.end(); ++iunit)
        stream << *iunit << "\n";
    stream << std::flush;
}

void print_nameMap(std::map<std::string,std::pair<uint32_t,std::map<std::string,uint32_t>>>& nameMap)
{
    for (std::map<std::string,std::pair<uint32_t,std::map<std::string,uint32_t>>>::const_iterator iunit = nameMap.begin(); iunit != nameMap.end(); ++iunit)
    {
        std::string h_name = iunit->first;
        std::pair<uint32_t,std::map<std::string,uint32_t>> value = iunit->second;
        uint32_t hid = value.first;
        std::map<std::string,uint32_t> vMap = value.second;
        std::cout << "H name: " << h_name << " id =" << hid << " vMap size:" << vMap.size() << "\n";
        for (std::map<std::string,uint32_t>::const_iterator junit = vMap.begin(); junit != vMap.end(); ++junit)
        {
            std::string v_name = junit->first;
            uint32_t vid = junit->second;
            std::cout << "V name: " << v_name << " id =" << vid << "\n";
        }
    }
}

//! \brief load_events: parse the evt config file.
//! \param fn:event config file name.
//! \param ofm: operation field map struct.
//! \param pfn_evtcb: see below.
//! \param evtcb_ctx: pointer of the callback context(user define).
//! \param nameMap: human readable metrics names.
//! \return -1 means fail, 0 means success.

//! \brief pfn_evtcb: call back func of event config file processing, app should provide it.
//! \param void *: pointer of the callback context.
//! \param counter &: common base counter struct.
//! \param std::map<std::string, uint32_t> &: operation field map struct.
//! \param std::string: event field name.
//! \param uint64: event field value.
//! \return -1 means fail with app exit, 0 means success or fail with continue.
int load_events(const std::string &fn, std::map<std::string, uint32_t> &ofm,
                int (*pfn_evtcb)(evt_cb_type, void *, counter &, std::map<std::string, uint32_t> &, std::string, uint64),
                void *evtcb_ctx, std::map<std::string,std::pair<uint32_t,std::map<std::string,uint32_t>>> &nameMap)
{
    struct counter ctr;

    std::ifstream in(fn);
    std::string line, item;
    if (!in.is_open())
    {
        const auto alt_fn = getInstallPathPrefix() + fn;
        in.open(alt_fn);
        if (!in.is_open())
        {
            in.close();
            const auto err_msg = std::string("event config file ") + fn + " or " + alt_fn + " is not available, you can try to manually copy it from PCM source package.";
            throw std::invalid_argument(err_msg);
        }
    }

    while (std::getline(in, line))
    {
        //TODO: substring until #, if len == 0, skip, else parse normally
        //Set default value if the item is NOT available in cfg file.
        ctr.h_event_name = "INVALID";
        ctr.v_event_name = "INVALID";
        ctr.ccr = 0;
        ctr.idx = 0;
        ctr.multiplier = 1;
        ctr.divider = 1;
        ctr.h_id = 0;
        ctr.v_id = 0;

        if (pfn_evtcb(EVT_LINE_START, evtcb_ctx, ctr, ofm, "", 0))
        {
            in.close();
            const auto err_msg = std::string("event line processing(start) fault.\n");
            throw std::invalid_argument(err_msg);
        }

        /* Ignore anyline with # */
        if (line.find("#") != std::string::npos)
            continue;
        /* If line does not have any deliminator, we ignore it as well */
        if (line.find("=") == std::string::npos)
            continue;

        std::string h_name, v_name;
        std::istringstream iss(line);
        while (std::getline(iss, item, ','))
        {
            std::string key, value;
            uint64 numValue;
            /* assume the token has the format <key>=<value> */
            key = item.substr(0,item.find("="));
            value = item.substr(item.find("=")+1);

            if (key.empty() || value.empty())
                continue; //skip the item if the token invalid

            std::istringstream iss2(value);
            iss2 >> std::setbase(0) >> numValue;

            switch (ofm[key])
            {
                case PCM::H_EVENT_NAME:
                    h_name = dos2unix(value);
                    ctr.h_event_name = h_name;
                    if (nameMap.find(h_name) == nameMap.end())
                    {
                        /* It's a new horizontal event name */
                        uint32_t next_h_id = (uint32_t)nameMap.size();
                        std::pair<uint32_t,std::map<std::string,uint32_t>> nameMap_value(next_h_id, std::map<std::string,uint32_t>());
                        nameMap[h_name] = nameMap_value;
                    }
                    ctr.h_id = (uint32_t)nameMap.size() - 1;
                    //cout << "h_name:" << ctr.h_event_name << "h_id: "<< ctr.h_id << "\n";
                    break;
                case PCM::V_EVENT_NAME:
                    {
                        v_name = dos2unix(value);
                        ctr.v_event_name = v_name;
                        //XXX: If h_name comes after v_name, we'll have a problem.
                        //XXX: It's very weird, I forgot to assign nameMap[h_name] = nameMap_value earlier (:298), but this part still works?
                        std::map<std::string,uint32_t> &v_nameMap = nameMap[h_name].second;
                        if (v_nameMap.find(v_name) == v_nameMap.end())
                        {
                            v_nameMap[v_name] = (unsigned int)v_nameMap.size() - 1;
                            //cout << "v_name(" << v_name << ")="<< v_nameMap[v_name] << "\n";
                        }
                        else
                        {
                            in.close();
                            const auto err_msg = std::string("Detect duplicated v_name:") + v_name + "\n";
                            throw std::invalid_argument(err_msg);
                        }
                        ctr.v_id = (uint32_t)v_nameMap.size() - 1;
                        //cout << "h_name:" << ctr.h_event_name << ",hid=" << ctr.h_id << ",v_name:" << ctr.v_event_name << ",v_id: "<< ctr.v_id << "\n";
                        break;
                    }
                //TODO: double type for multiplier. drop divider variable
                case PCM::MULTIPLIER:
                    ctr.multiplier = (int)numValue;
                    break;
                case PCM::DIVIDER:
                    ctr.divider = (int)numValue;
                    break;
                case PCM::COUNTER_INDEX:
                    ctr.idx = (int)numValue;
                    break;

                default:
                    if (pfn_evtcb(EVT_LINE_FIELD, evtcb_ctx, ctr, ofm, key, numValue))
                    {
                        in.close();
                        const auto err_msg = std::string("event line processing(field) fault.\n");
                        throw std::invalid_argument(err_msg);
                    }
                    break;
            }
        }

        //std::cout << "Finish parsing: " << line << "\n";
        if (pfn_evtcb(EVT_LINE_COMPLETE, evtcb_ctx, ctr, ofm, "", 0))
        {
            in.close();
            const auto err_msg = std::string("event line processing(end) fault.\n");
            throw std::invalid_argument(err_msg);
        }
    }

    //print_nameMap(nameMap); //DEBUG purpose
    in.close();
    return 0;
}

int load_events(const std::string &fn, std::map<std::string, uint32_t> &ofm,
                int (*pfn_evtcb)(evt_cb_type, void *, counter &, std::map<std::string, uint32_t> &, std::string, uint64),
                void *evtcb_ctx)
{
    std::map<std::string,std::pair<uint32_t,std::map<std::string,uint32_t>>> nm;
    return load_events(fn, ofm, pfn_evtcb, evtcb_ctx, nm);
}

bool get_cpu_bus(uint32 msmDomain, uint32 msmBus, uint32 msmDev, uint32 msmFunc, uint32 &cpuBusValid, std::vector<uint32> &cpuBusNo, int &cpuPackageId)
{
    //std::cout << "get_cpu_bus: d=" << std::hex << msmDomain << ",b=" << msmBus << ",d=" << msmDev << ",f=" << msmFunc << std::dec << " \n";
    try
    {
        PciHandleType h(msmDomain, msmBus, msmDev, msmFunc);

        h.read32(SPR_MSM_REG_CPUBUSNO_VALID_OFFSET, &cpuBusValid);
        if (cpuBusValid == (std::numeric_limits<uint32>::max)()) {
            std::cerr << "Failed to read CPUBUSNO_VALID" << std::endl;
            return false;
        }

        cpuBusNo.resize(8);
        for (int i = 0; i < 4; ++i) {
            h.read32(SPR_MSM_REG_CPUBUSNO0_OFFSET + i * 4, &cpuBusNo[i]);

            h.read32(SPR_MSM_REG_CPUBUSNO4_OFFSET + i * 4, &cpuBusNo[i + 4]);

            if (cpuBusNo[i] == (std::numeric_limits<uint32>::max)() ||
                cpuBusNo[i + 4] == (std::numeric_limits<uint32>::max)()) {
                std::cerr << "Failed to read CPUBUSNO registers" << std::endl;
                return false;
            }
        }

        /*
        * It's possible to have not enabled first stack that's why
        * need to find the first valid bus to read CSR
        */
        int firstValidBusId = 0;
        while (!((cpuBusValid >> firstValidBusId) & 0x1)) firstValidBusId++;
        int cpuBusNo0 = (cpuBusNo[(int)(firstValidBusId / 4)] >> ((firstValidBusId % 4) * 8)) & 0xff;

        uint32 sadControlCfg = 0x0;
        PciHandleType sad_cfg_handler(msmDomain, cpuBusNo0, 0, 0);
        sad_cfg_handler.read32(SPR_SAD_REG_CTL_CFG_OFFSET, &sadControlCfg);
        if (sadControlCfg == (std::numeric_limits<uint32>::max)()) {
            std::cerr << "Failed to read SAD_CONTROL_CFG" << std::endl;
            return false;
        }
        cpuPackageId = sadControlCfg & 0xf;

        return true;
    }
    catch (...)
    {
        std::cerr << "Warning: unable to enumerate CPU Buses" << std::endl;
        return false;
    }
}

std::pair<int64,int64> parseBitsParameter(const char * param)
{
    std::pair<int64,int64> bits{-1, -1};
    const auto bitsArray = pcm::split(std::string(param),':');
    assert(bitsArray.size() == 2);
    bits.first = (int64)read_number(bitsArray[0].c_str());
    bits.second = (int64)read_number(bitsArray[1].c_str());
    assert(bits.first >= 0);
    assert(bits.second >= 0);
    assert(bits.first < 64);
    assert(bits.second < 64);
    if (bits.first > bits.second)
    {
        std::swap(bits.first, bits.second);
    }
    return bits;
}

#ifdef __linux__
FILE * tryOpen(const char * path, const char * mode)
{
    FILE * f = fopen(path, mode);
    if (!f)
    {
        f = fopen((std::string("/pcm") + path).c_str(), mode);
    }
    return f;
}

std::string readSysFS(const char * path, bool silent)
{
    FILE * f = tryOpen(path, "r");
    if (!f)
    {
        if (silent == false) std::cerr << "ERROR: Can not open " << path << " file.\n";
        return std::string();
    }
    char buffer[1024];
    if(NULL == fgets(buffer, 1024, f))
    {
        if (silent == false) std::cerr << "ERROR: Can not read from " << path << ".\n";
        fclose(f);
        return std::string();
    }
    fclose(f);
    
    return std::string(buffer);
}

bool writeSysFS(const char * path, const std::string & value, bool silent)
{
    FILE * f = tryOpen(path, "w");
    if (!f)
    {
        if (silent == false) std::cerr << "ERROR: Can not open " << path << " file.\n";
        return false;
    }
    if (fputs(value.c_str(), f) < 0)
    {
        if (silent == false) std::cerr << "ERROR: Can not write to " << path << ".\n";
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

int readMaxFromSysFS(const char * path)
{
    std::string content = readSysFS(path);
    const char * buffer = content.c_str();
    int result = -1;
    pcm_sscanf(buffer) >> s_expect("0-") >> result;
    if(result == -1)
    {
       pcm_sscanf(buffer) >> result;
    }
    return result;
}

bool readMapFromSysFS(const char * path, std::unordered_map<std::string, uint32> &result, bool silent)
{
    FILE * f = tryOpen(path, "r");
    if (!f)
    {
        if (silent == false) std::cerr << "ERROR: Can not open " << path << " file.\n";
        return false;
    }
    char buffer[1024];
    while(fgets(buffer, 1024, f) != NULL)
    {
        std::string key, value, item;
        uint32 numValue = 0;

        item = std::string(buffer);
        key = item.substr(0,item.find(" "));
        value = item.substr(item.find(" ")+1);
        if (key.empty() || value.empty())
            continue; //skip the item if the token invalid
        std::istringstream iss2(value);
        iss2 >> std::setbase(0) >> numValue;
        result.insert(std::pair<std::string, uint32>(key, numValue));
        //std::cerr << "readMapFromSysFS:" << key << "=" << numValue << ".\n";
    }

    fclose(f);
    return true;
}
#endif

#ifdef _MSC_VER

//! restrict usage of driver to system (SY) and builtin admins (BA)
void restrictDriverAccessNative(LPCTSTR path)
{
    PSECURITY_DESCRIPTOR pSD = nullptr;

    if (!ConvertStringSecurityDescriptorToSecurityDescriptor(
        _T("O:BAG:SYD:(A;;FA;;;SY)(A;;FA;;;BA)"),
        SDDL_REVISION_1,
        &pSD,
        nullptr))
    {
        _tprintf(TEXT("Error in ConvertStringSecurityDescriptorToSecurityDescriptor: %d\n"), GetLastError());
        return;
    }

    if (SetFileSecurity(path, DACL_SECURITY_INFORMATION, pSD))
    {
        // _tprintf(TEXT("Successfully restricted access for %s\n"), path);
    }
    else
    {
        _tprintf(TEXT("Error in SetFileSecurity for %s. Error %d\n"), path, GetLastError());
    }

    LocalFree(pSD);
}
#endif

} // namespace pcm

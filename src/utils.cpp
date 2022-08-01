// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2018, Intel Corporation
// written by Andrey Semin and many others

#include <iostream>
#include <cassert>
#include <climits>
#ifdef _MSC_VER
#include <process.h>
#include <comdef.h>
#else
#include <sys/wait.h> // for waitpid()
#include <unistd.h> // for ::sleep
#endif
#include "utils.h"
#include "cpucounters.h"

namespace pcm {

void (*post_cleanup_callback)(void) = NULL;

//! \brief handler of exit() call
void exit_cleanup(void)
{
    std::cout << std::flush;

    restore_signal_handlers();

    // this replaces same call in cleanup() from util.h
    PCM::getInstance()->cleanup(); // this replaces same call in cleanup() from util.h

//TODO: delete other shared objects.... if any.

    if(post_cleanup_callback != NULL)
    {
        post_cleanup_callback();
    }
}

void print_cpu_details()
{
    const auto m = PCM::getInstance();
    std::cerr << "\nDetected " << m->getCPUBrandString() << " \"Intel(r) microarchitecture codename " <<
        m->getUArchCodename() << "\" stepping " << m->getCPUStepping();
    const auto ucode_level = m->getCPUMicrocodeLevel();
    if (ucode_level >= 0)
    {
        std::cerr << " microcode level 0x" << std::hex << ucode_level;
    }
    std::cerr << "\n";
}

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
    if (PCM::getInstance()->isBlocked()) {
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
    if (PCM::getInstance()->isBlocked()) {
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
    free(envPath);
    if (envPath)
    {
        std::cerr << "\nPCM ERROR: Detected cygwin/mingw environment which does not allow to setup PMU clean-up handlers on Ctrl-C and other termination signals.\n";
        std::cerr << "See https://www.mail-archive.com/cygwin@cygwin.com/msg74817.html\n";
        std::cerr << "As a workaround please run pcm directly from a native windows shell (e.g. cmd).\n";
        std::cerr << "Exiting...\n\n";
        _exit(EXIT_FAILURE);
    }
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
    sigaction(SIGSEGV, &saINT, NULL);

    saINT.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &saINT, NULL); // get there is our child exits. do nothing if it stopped/continued

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
            std::cerr << "Program " << sysCmd << " launched with PID: " << child_pid << "\n";

            if (WIFEXITED(res)) {
                std::cerr << "Program exited with status " << WEXITSTATUS(res) << "\n";
            }
            else if (WIFSIGNALED(res)) {
                std::cerr << "Process " << child_pid << " was terminated with status " << WTERMSIG(res);
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

void print_help_force_rtm_abort_mode(const int alignment)
{
    const auto m = PCM::getInstance();
    if (m->isForceRTMAbortModeAvailable() && (m->getMaxCustomCoreEvents() < 4))
    {
        std::cout << "  -force-rtm-abort-mode";
        for (int i = 0; i < (alignment - 23); ++i)
        {
            std::cout << " ";
        }
        std::cout << "=> force RTM transaction abort mode to enable more programmable counters\n";
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
        free(buffer);
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

} // namespace pcm

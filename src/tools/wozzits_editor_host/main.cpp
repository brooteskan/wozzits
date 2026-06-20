#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <Windows.h>

#include <engine/project/project_runtime_launch.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace
{
    struct Options
    {
        wz::fs::Path project_root;
        wz::fs::Path resource_root;
    };

    struct ChildProcess
    {
        PROCESS_INFORMATION process{};
        HANDLE stdout_read = nullptr;
        HANDLE stderr_read = nullptr;
    };

    struct WindowCloseSearch
    {
        DWORD process_id = 0;
        bool posted = false;
    };

    constexpr ULONGLONG kRuntimeShutdownGraceMs = 30000u;

    std::wstring utf8_to_wide(const std::string& str)
    {
        if (str.empty()) {
            return {};
        }

        const int size_needed = MultiByteToWideChar(
            CP_UTF8,
            0,
            str.c_str(),
            static_cast<int>(str.size()),
            nullptr,
            0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(
            CP_UTF8,
            0,
            str.c_str(),
            static_cast<int>(str.size()),
            wstr.data(),
            size_needed);
        return wstr;
    }

    std::wstring quote_command_arg(const std::string& arg)
    {
        std::wstring wide = utf8_to_wide(arg);
        std::wstring quoted;
        quoted.reserve(wide.size() + 2);
        quoted.push_back(L'"');

        unsigned backslashes = 0;
        for (const wchar_t ch : wide) {
            if (ch == L'\\') {
                ++backslashes;
                continue;
            }

            if (ch == L'"') {
                quoted.append(backslashes * 2 + 1, L'\\');
                quoted.push_back(ch);
                backslashes = 0;
                continue;
            }

            quoted.append(backslashes, L'\\');
            backslashes = 0;
            quoted.push_back(ch);
        }

        quoted.append(backslashes * 2, L'\\');
        quoted.push_back(L'"');
        return quoted;
    }

    void write_log(const char* level, const std::string& message)
    {
        std::cout << '[' << level << "] " << message << '\n';
        std::cout.flush();
    }

    std::string windows_error_message(const std::string& action)
    {
        return action + " failed with Win32 error "
            + std::to_string(GetLastError());
    }

    bool create_inheritable_pipe(
        HANDLE& read_handle,
        HANDLE& write_handle,
        std::string& error)
    {
        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;

        if (!CreatePipe(&read_handle, &write_handle, &security, 0)) {
            error = windows_error_message("CreatePipe");
            return false;
        }
        if (!SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0)) {
            error = windows_error_message("SetHandleInformation");
            CloseHandle(read_handle);
            CloseHandle(write_handle);
            read_handle = nullptr;
            write_handle = nullptr;
            return false;
        }
        return true;
    }

    wz::fs::Path runtime_app_executable_path()
    {
        const wz::fs::Path host_exe = wz::fs::executable_path();
        const wz::fs::Path host_dir = wz::fs::parent_path(host_exe);
        return wz::fs::join(host_dir, "wozzits_app_v1.exe");
    }

    void relay_pipe_to_stdout(HANDLE read_handle, std::mutex& output_mutex)
    {
        char buffer[4096];
        DWORD bytes_read = 0;
        while (ReadFile(
            read_handle,
            buffer,
            static_cast<DWORD>(sizeof(buffer)),
            &bytes_read,
            nullptr)
            && bytes_read > 0)
        {
            std::lock_guard lock(output_mutex);
            std::cout.write(buffer, static_cast<std::streamsize>(bytes_read));
            std::cout.flush();
        }
        CloseHandle(read_handle);
    }

    BOOL CALLBACK post_close_to_process_window(HWND hwnd, LPARAM user)
    {
        auto* search = reinterpret_cast<WindowCloseSearch*>(user);
        DWORD window_process_id = 0;
        GetWindowThreadProcessId(hwnd, &window_process_id);
        if (window_process_id == search->process_id) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            search->posted = true;
            return FALSE;
        }
        return TRUE;
    }

    bool request_process_window_close(DWORD process_id)
    {
        WindowCloseSearch search{ .process_id = process_id };
        EnumWindows(post_close_to_process_window, reinterpret_cast<LPARAM>(&search));
        return search.posted;
    }

    bool start_runtime_app(
        const wz::engine::project::ProjectRuntimeLaunch& launch,
        ChildProcess& child,
        std::string& error)
    {
        HANDLE stdout_write = nullptr;
        HANDLE stderr_write = nullptr;
        HANDLE stdin_read = nullptr;

        if (!create_inheritable_pipe(
                child.stdout_read,
                stdout_write,
                error))
        {
            return false;
        }
        if (!create_inheritable_pipe(
                child.stderr_read,
                stderr_write,
                error))
        {
            CloseHandle(child.stdout_read);
            CloseHandle(stdout_write);
            child.stdout_read = nullptr;
            return false;
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        stdin_read = CreateFileW(
            L"NUL",
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            &security,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (stdin_read == INVALID_HANDLE_VALUE) {
            error = windows_error_message("CreateFileW(NUL)");
            CloseHandle(child.stdout_read);
            CloseHandle(stdout_write);
            CloseHandle(child.stderr_read);
            CloseHandle(stderr_write);
            child.stdout_read = nullptr;
            child.stderr_read = nullptr;
            return false;
        }

        const wz::fs::Path app_exe = runtime_app_executable_path();
        std::wstring command = quote_command_arg(app_exe)
            + L" --resource-root " + quote_command_arg(launch.resource_root)
            + L" --asset-graph " + quote_command_arg(launch.asset_graph_path)
            + L" --scene " + quote_command_arg(launch.scene_path);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = stdin_read;
        startup.hStdOutput = stdout_write;
        startup.hStdError = stderr_write;

        const BOOL ok = CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            nullptr,
            &startup,
            &child.process);

        CloseHandle(stdin_read);
        CloseHandle(stdout_write);
        CloseHandle(stderr_write);

        if (!ok) {
            error = windows_error_message("CreateProcessW");
            CloseHandle(child.stdout_read);
            CloseHandle(child.stderr_read);
            child.stdout_read = nullptr;
            child.stderr_read = nullptr;
            return false;
        }

        return true;
    }

    bool parse_options(int argc, char** argv, Options& out, std::string& error)
    {
        if (argc < 3) {
            error =
                "usage: wozzits_editor_host "
                "serve <project-root> [--resource-root <path>]";
            return false;
        }

        const std::string command = argv[1];
        if (command != "serve") {
            error = "unknown command: " + command
                + " (only serve is supported)";
            return false;
        }

        out.project_root = argv[2];

        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--resource-root" && i + 1 < argc) {
                out.resource_root = argv[++i];
            }
            else {
                error = "unknown or incomplete option: " + arg;
                return false;
            }
        }
        return true;
    }
}

int main(int argc, char** argv)
{
    Options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        write_log("error", error);
        return 2;
    }

    write_log("info", "wozzits_editor_host starting");
    write_log("info", "project root: " + options.project_root);

    const auto result = wz::engine::project::load_project_runtime_launch(
        wz::engine::project::ProjectRuntimeLaunchDesc{
            .project_root = options.project_root,
            .resource_root = options.resource_root,
        });

    if (!result.ok) {
        write_log("error", result.error);
        return 1;
    }

    write_log("info", "project loaded: " + result.launch.manifest.name);

    ChildProcess runtime;
    std::string launch_error;
    if (!start_runtime_app(result.launch, runtime, launch_error)) {
        write_log("error", launch_error);
        return 1;
    }

    write_log("info", "runtime app launched: " + runtime_app_executable_path());
    write_log("info", "ready");

    std::mutex output_mutex;
    std::thread stdout_thread(
        relay_pipe_to_stdout,
        runtime.stdout_read,
        std::ref(output_mutex));
    std::thread stderr_thread(
        relay_pipe_to_stdout,
        runtime.stderr_read,
        std::ref(output_mutex));

    auto shutdown_requested = std::make_shared<std::atomic_bool>(false);
    std::thread input_thread([shutdown_requested]() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "shutdown" || line == "exit" || line == "quit") {
                shutdown_requested->store(true, std::memory_order_release);
                return;
            }
        }
        shutdown_requested->store(true, std::memory_order_release);
    });
    input_thread.detach();

    bool shutdown_logged = false;
    ULONGLONG shutdown_requested_at = 0;
    bool close_requested = false;
    ULONGLONG close_requested_at = 0;
    DWORD wait_result = WAIT_TIMEOUT;
    while ((wait_result = WaitForSingleObject(runtime.process.hProcess, 100))
        == WAIT_TIMEOUT)
    {
        const bool wants_shutdown =
            shutdown_requested->load(std::memory_order_acquire);
        if (wants_shutdown && !shutdown_logged) {
            write_log("info", "shutdown requested");
            shutdown_requested_at = GetTickCount64();
            shutdown_logged = true;
        }

        if (wants_shutdown && !close_requested) {
            if (request_process_window_close(runtime.process.dwProcessId)) {
                close_requested = true;
                close_requested_at = GetTickCount64();
            }
            else if (GetTickCount64() - shutdown_requested_at
                > kRuntimeShutdownGraceMs)
            {
                write_log(
                    "warn",
                    "runtime app window was not found; terminating");
                TerminateProcess(runtime.process.hProcess, 1);
                WaitForSingleObject(runtime.process.hProcess, INFINITE);
                break;
            }
        }

        if (close_requested
            && GetTickCount64() - close_requested_at
                > kRuntimeShutdownGraceMs)
        {
            write_log("warn", "runtime app did not exit; terminating");
            TerminateProcess(runtime.process.hProcess, 1);
            WaitForSingleObject(runtime.process.hProcess, INFINITE);
            break;
        }
    }

    DWORD exit_code = 1;
    GetExitCodeProcess(runtime.process.hProcess, &exit_code);
    CloseHandle(runtime.process.hThread);
    CloseHandle(runtime.process.hProcess);

    stdout_thread.join();
    stderr_thread.join();

    write_log(
        "info",
        "runtime app exited with code " + std::to_string(exit_code));
    return exit_code == 0 ? 0 : 1;
}

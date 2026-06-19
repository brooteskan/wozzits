using System.Diagnostics;
using System.Text.Json;
using Wozzits.Editor.Protocol;

namespace Wozzits.Editor.HostClient;

public sealed class WozzitsEditorHostClient
{
    public const string HostPathEnvironmentVariable = "WOZZITS_EDITOR_HOST";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    private readonly string _hostExecutablePath;

    public WozzitsEditorHostClient(string? hostExecutablePath = null)
    {
        _hostExecutablePath = string.IsNullOrWhiteSpace(hostExecutablePath)
            ? ResolveDefaultHostExecutablePath()
            : hostExecutablePath;
    }

    public string HostExecutablePath => _hostExecutablePath;

    public EngineProjectResponse ProbeProject(string projectDirectory)
    {
        return Invoke("probe", projectDirectory, name: null);
    }

    public EngineProjectResponse CreateProject(string projectDirectory, string? name = null)
    {
        return Invoke("create", projectDirectory, name);
    }

    private EngineProjectResponse Invoke(string command, string projectDirectory, string? name)
    {
        if (!File.Exists(_hostExecutablePath))
        {
            return Invalid($"Editor host executable not found: {_hostExecutablePath}");
        }

        var startInfo = new ProcessStartInfo
        {
            FileName = _hostExecutablePath,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add(command);
        startInfo.ArgumentList.Add(projectDirectory);
        if (!string.IsNullOrWhiteSpace(name))
        {
            startInfo.ArgumentList.Add("--name");
            startInfo.ArgumentList.Add(name);
        }

        try
        {
            using var process = Process.Start(startInfo);
            if (process is null)
            {
                return Invalid($"Could not start editor host: {_hostExecutablePath}");
            }

            var stdout = process.StandardOutput.ReadToEnd();
            var stderr = process.StandardError.ReadToEnd();
            if (!process.WaitForExit(milliseconds: 10_000))
            {
                process.Kill(entireProcessTree: true);
                return Invalid("Editor host timed out.");
            }

            var response = JsonSerializer.Deserialize<EngineProjectResponse>(stdout, JsonOptions);
            if (response is null)
            {
                return Invalid("Editor host returned an empty response.");
            }

            if (process.ExitCode != 0 && string.IsNullOrWhiteSpace(response.Error))
            {
                return response with
                {
                    Error = string.IsNullOrWhiteSpace(stderr)
                        ? $"Editor host failed with exit code {process.ExitCode}."
                        : stderr.Trim(),
                };
            }

            return response;
        }
        catch (Exception ex)
        {
            return Invalid(ex.Message);
        }
    }

    private static EngineProjectResponse Invalid(string error)
    {
        return new EngineProjectResponse
        {
            Ok = false,
            Status = EngineProjectStatus.Invalid,
            Error = error,
        };
    }

    public static string ResolveDefaultHostExecutablePath()
    {
        var configured = Environment.GetEnvironmentVariable(HostPathEnvironmentVariable);
        if (!string.IsNullOrWhiteSpace(configured))
        {
            return configured;
        }

        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory is not null)
        {
            if (string.Equals(directory.Name, "wozzits-editor", StringComparison.OrdinalIgnoreCase)
                && directory.Parent is not null)
            {
                return Path.Combine(
                    directory.Parent.FullName,
                    "wozzits-window-engine",
                    "build",
                    "clang-debug",
                    "wozzits_editor_host.exe");
            }

            directory = directory.Parent;
        }

        return "wozzits_editor_host.exe";
    }
}

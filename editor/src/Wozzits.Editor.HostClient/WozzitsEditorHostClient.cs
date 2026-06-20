namespace Wozzits.Editor.HostClient;

public sealed class WozzitsEditorHostClient
{
    public const string HostPathEnvironmentVariable = "WOZZITS_EDITOR_HOST";

    private readonly string _hostExecutablePath;

    public WozzitsEditorHostClient(string? hostExecutablePath = null)
    {
        _hostExecutablePath = string.IsNullOrWhiteSpace(hostExecutablePath)
            ? ResolveDefaultHostExecutablePath()
            : hostExecutablePath;
    }

    public string HostExecutablePath => _hostExecutablePath;

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

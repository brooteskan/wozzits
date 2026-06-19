namespace Wozzits.Editor.Core.Projects;

public sealed record ProjectLaunchOptions(string? ProjectDirectory)
{
    public static ProjectLaunchOptions FromCommandLine(IEnumerable<string> args)
    {
        ArgumentNullException.ThrowIfNull(args);

        var projectDirectory = args
            .FirstOrDefault(arg => !string.IsNullOrWhiteSpace(arg) && !arg.StartsWith("-", StringComparison.Ordinal));

        return new ProjectLaunchOptions(projectDirectory);
    }
}

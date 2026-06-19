using System.IO;

namespace Wozzits.Editor.Core.Projects;

public sealed record ProjectPath
{
    public ProjectPath(string value)
    {
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException("Project path cannot be empty.", nameof(value));
        }

        Value = value;
    }

    public string Value { get; }

    public bool IsAbsolute => Path.IsPathFullyQualified(Value);

    public string Resolve(ProjectDirectory projectDirectory)
    {
        ArgumentNullException.ThrowIfNull(projectDirectory);

        return IsAbsolute
            ? Path.GetFullPath(Value)
            : Path.GetFullPath(Path.Combine(projectDirectory.FullPath, Value));
    }
}

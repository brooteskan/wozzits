using System.IO;

namespace Wozzits.Editor.Core.Projects;

public sealed record ProjectDirectory
{
    public ProjectDirectory(string fullPath)
    {
        if (string.IsNullOrWhiteSpace(fullPath))
        {
            throw new ArgumentException("Project directory cannot be empty.", nameof(fullPath));
        }

        FullPath = Path.GetFullPath(fullPath);
    }

    public string FullPath { get; }
}

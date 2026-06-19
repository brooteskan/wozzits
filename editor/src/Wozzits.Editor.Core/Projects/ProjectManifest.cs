namespace Wozzits.Editor.Core.Projects;

public sealed record ProjectManifest(int FormatVersion)
{
    public const int CurrentFormatVersion = 1;

    public static ProjectManifest CreateDefault()
    {
        return new ProjectManifest(CurrentFormatVersion);
    }
}

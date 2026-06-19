using System.IO;
using System.Text.Json;

namespace Wozzits.Editor.Core.Projects;

public sealed class ProjectFileSet
{
    public const string MetadataDirectoryName = ".wozzits";
    public const string ManifestFileName = "project.json";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
    };

    public ProjectFileSet(ProjectDirectory projectDirectory)
    {
        ProjectDirectory = projectDirectory ?? throw new ArgumentNullException(nameof(projectDirectory));
    }

    public ProjectDirectory ProjectDirectory { get; }

    public string MetadataDirectoryPath => Path.Combine(ProjectDirectory.FullPath, MetadataDirectoryName);

    public string ManifestPath => Path.Combine(MetadataDirectoryPath, ManifestFileName);

    public bool Exists()
    {
        return File.Exists(ManifestPath);
    }

    public ProjectManifest Create()
    {
        Directory.CreateDirectory(ProjectDirectory.FullPath);
        Directory.CreateDirectory(MetadataDirectoryPath);

        if (Exists())
        {
            return ReadManifest();
        }

        var manifest = ProjectManifest.CreateDefault();
        var json = JsonSerializer.Serialize(manifest, JsonOptions);
        File.WriteAllText(ManifestPath, json + Environment.NewLine);

        return manifest;
    }

    public ProjectManifest ReadManifest()
    {
        using var stream = File.OpenRead(ManifestPath);
        return JsonSerializer.Deserialize<ProjectManifest>(stream, JsonOptions)
            ?? throw new InvalidDataException($"Project manifest is empty: {ManifestPath}");
    }
}

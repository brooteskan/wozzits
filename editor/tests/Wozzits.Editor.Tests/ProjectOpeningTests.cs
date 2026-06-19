using System.IO;
using Wozzits.Editor.Core.Projects;

namespace Wozzits.Editor.Tests;

public sealed class ProjectOpeningTests
{
    [Fact]
    public void LaunchOptionsUseFirstPositionalArgumentAsProjectDirectory()
    {
        var options = ProjectLaunchOptions.FromCommandLine(["--verbose", @"D:\work\project"]);

        Assert.Equal(@"D:\work\project", options.ProjectDirectory);
    }

    [Fact]
    public void RelativeProjectPathsResolveInsideProjectDirectory()
    {
        var projectDirectory = new ProjectDirectory(@"D:\work\project");
        var path = new ProjectPath(@"assets\scene.wozzit");

        Assert.Equal(
            Path.GetFullPath(@"D:\work\project\assets\scene.wozzit"),
            path.Resolve(projectDirectory));
    }

    [Fact]
    public void AbsoluteProjectPathsResolveToThemselves()
    {
        var projectDirectory = new ProjectDirectory(@"D:\work\project");
        var path = new ProjectPath(@"E:\shared\textures\stone.png");

        Assert.Equal(
            Path.GetFullPath(@"E:\shared\textures\stone.png"),
            path.Resolve(projectDirectory));
    }

    [Fact]
    public void ProjectFilesLiveUnderPassedProjectDirectory()
    {
        var projectDirectory = new ProjectDirectory(@"D:\work\project");
        var projectFiles = new ProjectFileSet(projectDirectory);

        Assert.Equal(
            Path.GetFullPath(@"D:\work\project\.wozzits\project.json"),
            projectFiles.ManifestPath);
    }

    [Fact]
    public void CreateProjectFilesCreatesManifestInsidePassedDirectory()
    {
        var projectRoot = Path.Combine(Path.GetTempPath(), "wozzits-editor-tests", Guid.NewGuid().ToString("N"));
        var projectDirectory = new ProjectDirectory(projectRoot);
        var projectFiles = new ProjectFileSet(projectDirectory);

        try
        {
            Assert.False(projectFiles.Exists());

            var manifest = projectFiles.Create();

            Assert.Equal(ProjectManifest.CurrentFormatVersion, manifest.FormatVersion);
            Assert.True(projectFiles.Exists());
            Assert.True(File.Exists(Path.Combine(projectRoot, ".wozzits", "project.json")));
        }
        finally
        {
            if (Directory.Exists(projectRoot))
            {
                Directory.Delete(projectRoot, recursive: true);
            }
        }
    }
}

using System.IO;
using Wozzits.Editor.Core.Projects;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels;

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
    public void BootstrapAllowsCreateOnlyWhenEngineReportsMissingProject()
    {
        var viewModel = new ProjectBootstrapViewModel(
            new ProjectDirectory(@"D:\work\project"),
            new WozzitsEditorHostClient("unused.exe"),
            new EngineProjectResponse
            {
                Status = EngineProjectStatus.Missing,
                Error = "missing",
            },
            openProject: _ => { },
            quit: () => { });

        Assert.True(viewModel.CanCreateProject);
    }

    [Fact]
    public void BootstrapDisablesCreateWhenEngineReportsInvalidProject()
    {
        var viewModel = new ProjectBootstrapViewModel(
            new ProjectDirectory(@"D:\work\project"),
            new WozzitsEditorHostClient("unused.exe"),
            new EngineProjectResponse
            {
                Status = EngineProjectStatus.Invalid,
                Error = "bad project",
            },
            openProject: _ => { },
            quit: () => { });

        Assert.False(viewModel.CanCreateProject);
        Assert.Equal("bad project", viewModel.ErrorMessage);
    }
}

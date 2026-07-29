using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.ViewModels;

namespace Wozzits.Editor.Tests;

// The scene lives in the running engine, so "save the scene" is only meaningful
// while a viewport runs. When it is not running the save must FAIL and the
// failure must reach the user: reporting success loses every scene edit made
// since the viewport closed, with no error, no log line, and no dirty marker.
public sealed partial class ProjectOpeningTests
{
    [Fact]
    public void SaveSceneWithoutRuntimeReportsFailureNotPhantomSuccess()
    {
        // No runtime: the session short-circuits before touching the native ABI,
        // so this exercises the real class without an engine.
        var session = new WozzitsEngineNativeEditorSession(
            new WozzitsEngineNativeClient(),
            @"D:\work\project",
            IntPtr.Zero,
            runtime: null);

        var response = session.SaveScene();

        Assert.False(response.Ok);
        Assert.NotEmpty(response.Error);
    }

    [Fact]
    public void SaveAllSurfacesASceneThatDidNotSave()
    {
        var editorSession = new RecordingEditorSession { RuntimeRunning = false };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(),
            editorSession: editorSession);

        viewModel.SaveAllCommand.Execute(null);

        Assert.Equal(1, editorSession.SaveSceneCount);
        Assert.Contains("Scene NOT saved", viewModel.EngineLogText);
    }

    [Fact]
    public void SaveAllStaysQuietWhenTheSceneSaves()
    {
        var editorSession = new RecordingEditorSession { RuntimeRunning = true };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(),
            editorSession: editorSession);

        viewModel.SaveAllCommand.Execute(null);

        Assert.Equal(1, editorSession.SaveSceneCount);
        Assert.DoesNotContain("Scene NOT saved", viewModel.EngineLogText);
    }
}

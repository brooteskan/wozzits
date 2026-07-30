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

    // A1-C6: the scene-mutation verbs return OK before the engine thread resolves
    // the node id, so an edit aimed at a node a graph swap / despawn / open_scene
    // removed used to disappear with the caller told it succeeded. Save is where
    // that has to surface -- we are about to persist a state that does not contain
    // the edit the user believes they made.
    [Fact]
    public void SaveAllSurfacesAnEditTheEngineDropped()
    {
        var editorSession = new RecordingEditorSession { RuntimeRunning = true };
        editorSession.DroppedEdits.Add(
            "set_node_properties: node 'ghost' is not in the running scene, "
            + "so the edit was dropped");
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(),
            editorSession: editorSession);

        viewModel.SaveAllCommand.Execute(null);

        Assert.Contains("Edit did NOT apply", viewModel.EngineLogText);
        Assert.Contains("ghost", viewModel.EngineLogText);

        // TAKE semantics all the way through: a second Save must not repeat a drop
        // that was already reported, or every later save re-accuses the same node.
        var before = viewModel.EngineLogText;
        viewModel.SaveAllCommand.Execute(null);
        var added = viewModel.EngineLogText.Substring(before.Length);
        Assert.DoesNotContain("Edit did NOT apply", added);
    }

    [Fact]
    public void SaveAllStaysQuietWhenNothingWasDropped()
    {
        var editorSession = new RecordingEditorSession { RuntimeRunning = true };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(),
            editorSession: editorSession);

        viewModel.SaveAllCommand.Execute(null);

        Assert.DoesNotContain("Edit did NOT apply", viewModel.EngineLogText);
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

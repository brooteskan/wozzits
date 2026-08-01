using Dock.Model.Core;
using Dock.Model.Mvvm.Controls;
using Wozzits.Editor.HostClient;
using Wozzits.Editor.Protocol;
using Wozzits.Editor.Statecharts;
using Wozzits.Editor.ViewModels;
using Wozzits.Editor.ViewModels.EditorPanes.Minds;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

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

    // D3-C6. A mind with no qubits is two clicks away (the pane has no floor on
    // DeleteSelected), and MindJson.Emit refuses it. SaveOpenMinds caught only
    // IOException/UnauthorizedAccessException, so MindFormatException escaped
    // SaveAll -- a SYNCHRONOUS RelayCommand with no global unhandled handler
    // anywhere in the editor -- and killed the process, losing every OTHER
    // unsaved document too.
    //
    // Assert.Null(record) is the load-bearing half: on the unfixed code this
    // test does not merely fail an assertion, the exception escapes here. The
    // log assertion pins the second half of the contract (the statechart twin's
    // comment calls it "load-bearing"): a mind that did not save must stay OUT
    // of savedMinds, or the refresh pushes IR the engine refuses into every
    // attached quantum_agent and into the scenelet FILES.
    [Fact]
    public void SaveAllSurvivesAMindThatCannotBeEmitted()
    {
        var dir = Path.Combine(
            Path.GetTempPath(), "wz_saveall_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(dir);
        try
        {
            var path = Path.Combine(dir, "broken.mind.json");
            var mind = new Mind { Name = "broken" };
            mind.Qubits.Add(new MindQubit { Id = "q0" });
            File.WriteAllText(path, MindJson.Emit(mind, indented: true));

            var viewModel = new MainWindowViewModel(
                ProjectSnapshot(),
                editorSession: new RecordingEditorSession { RuntimeRunning = true },
                projectDirectory: dir);
            viewModel.OpenMindCommand.Execute(new MindFileInfo("broken", path));

            var document = viewModel.EditorLayout is null
                ? null
                : OpenMindDocument(viewModel);
            Assert.NotNull(document);

            // Delete the last qubit -- the pane allows it, and Emit then refuses.
            document!.Pane.SelectOnly(document.Pane.Nodes[0]);
            document.Pane.DeleteSelected();
            Assert.True(document.IsDirty);

            var record = Record.Exception(() => viewModel.SaveAllCommand.Execute(null));

            Assert.Null(record);
            Assert.Contains("Mind 'broken' NOT saved", viewModel.EngineLogText);
        }
        finally
        {
            Directory.Delete(dir, recursive: true);
        }
    }

    // D3-C16. SimulationPaused is resynced after a viewport restart (with a
    // comment saying why); FrameProfilingEnabled, an identically-shaped toggle
    // twelve lines away in the same file, was not. The fresh runtime came back
    // with profiling off while the _Run menu still showed it checked -- and
    // clicking that menu item could not fix it, because SetProperty short-circuits
    // on an unchanged value, so the user had to toggle off and then on again.
    [Fact]
    public void RestartingTheViewportReappliesFrameProfiling()
    {
        var editorSession = new RecordingEditorSession { RuntimeRunning = true };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(),
            editorSession: editorSession);

        viewModel.FrameProfilingEnabled = true;
        Assert.Equal([true], editorSession.FrameProfilingToggles);

        viewModel.RestartViewportCommand.Execute(null);

        Assert.Equal(1, editorSession.RestartRuntimeCount);
        // The re-push is the fix: without it the fresh runtime keeps profiling off
        // while the property -- and so the menu's check mark -- still reads true.
        Assert.Equal([true, true], editorSession.FrameProfilingToggles);
        Assert.True(viewModel.FrameProfilingEnabled);
    }

    // D3-C17. "Rebuild Behavior Modules" calls RefreshDeclaredParams, which
    // re-Inspected the CACHED scene node whenever one had ever been selected --
    // even though selecting an asset-graph node (or a statechart state) leaves
    // _inspectedSceneNode set and only changes the selection KIND. So a rebuild
    // yanked the pane back to a node the user was no longer looking at, throwing
    // away anything typed into an Apply-gated field on the way. Its sibling
    // RefreshBehaviorModuleCatalog already gated on HasSceneNodeSelection.
    [Fact]
    public void RefreshingDeclaredParamsDoesNotHijackANonSceneSelection()
    {
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children: [Node("mesh", parentId: "root", visible: true)]))),
            editorSession: new RecordingEditorSession());

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var mesh = Assert.Single(root.Children, n => n.Id == "mesh");

        // Select a scene node first -- that is what populates the cache.
        viewModel.SceneTree.SelectNode(mesh);
        Assert.True(viewModel.Inspector.HasSceneNodeSelection);

        // Then select a statechart state. This is the path that matters: unlike
        // the asset-graph path (which clears the cached scene node, and so is
        // ALREADY safe -- writing this test against it makes it vacuous), the
        // statechart overloads only change the selection kind.
        viewModel.Inspector.Inspect(
            new StateNodeViewModel(new State { Id = "s1" }, isInitial: true));
        Assert.True(viewModel.Inspector.HasStatechartStateSelection);
        Assert.False(viewModel.Inspector.HasSceneNodeSelection);

        viewModel.Inspector.RefreshDeclaredParams();

        // The pane must stay where the user left it.
        Assert.True(viewModel.Inspector.HasStatechartStateSelection);
        Assert.False(viewModel.Inspector.HasSceneNodeSelection);
    }

    // D3-C13. SetEditResponse used to return void, so every Apply*/On*FieldEdited
    // pair recorded the engine's answer into LastEditError and then mirrored the
    // edit onto the cached tree node REGARDLESS. A rejected edit therefore showed
    // as applied -- and reselecting the node before the next snapshot refresh
    // confirmed the lie, because the mirror is exactly what a reselect reads.
    [Fact]
    public void ARejectedComponentEditIsNotMirroredOntoTheCachedNode()
    {
        var editorSession = new RecordingEditorSession
        {
            RuntimeRunning = true,
            RejectComponentEditsWith = "collision node is not in the running scene",
        };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                scene: SceneSnapshot(
                    Node(
                        "root",
                        children:
                        [
                            Node(
                                "node",
                                parentId: "root",
                                visible: true,
                                components: [Component("collision", "Collision")],
                                collision: new EngineSceneNodeCollision
                                {
                                    ConstrainMovement = false,
                                }),
                        ]))),
            editorSession: editorSession);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var node = Assert.Single(root.Children, n => n.Id == "node");
        viewModel.SceneTree.SelectNode(node);
        Assert.False(viewModel.Inspector.CollisionConstrainMovement);

        viewModel.Inspector.CollisionConstrainMovement = true;

        // The engine WAS asked -- this is a rejection, not a short-circuit.
        Assert.Single(editorSession.Collisions);
        // ...and it said no, so the user is told...
        Assert.Equal(
            "collision node is not in the running scene",
            viewModel.Inspector.LastEditError);
        // ...and the cached node must still hold the pre-edit value, or a reselect
        // would show an edit the engine never accepted.
        Assert.False(node.Collision?.ConstrainMovement ?? false);
    }

    private static MindDocumentViewModel? OpenMindDocument(MainWindowViewModel viewModel)
    {
        return viewModel.DockFactory is null
            ? null
            : FindMindDocuments(viewModel.EditorLayout).FirstOrDefault();
    }

    private static IEnumerable<MindDocumentViewModel> FindMindDocuments(object? dockable)
    {
        if (dockable is Document { Context: MindDocumentViewModel mind })
        {
            yield return mind;
        }

        if (dockable is IDock dock && dock.VisibleDockables is not null)
        {
            foreach (var child in dock.VisibleDockables)
            {
                foreach (var found in FindMindDocuments(child))
                {
                    yield return found;
                }
            }
        }
    }
}

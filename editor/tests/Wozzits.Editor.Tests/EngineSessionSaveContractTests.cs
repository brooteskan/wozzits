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

    // D3-C12. Nine dedicated remove handlers ran the engine call inside
    // `if (EnsureCanApply()) { ... }` and then dropped the component from the
    // editor's own model UNCONDITIONALLY -- so with the viewport closed the ✕
    // still "worked": the section vanished, the engine was never asked, and the
    // component was still there on the next snapshot. RemoveCameraComponent was
    // the one handler that already returned early and gated on response.Ok; these
    // nine now match it.
    [Theory]
    [InlineData(false, "the viewport is down, so the engine is never asked")]
    [InlineData(true, "the engine is asked and refuses")]
    public void RemovingAComponentTheEngineDidNotRemoveKeepsItInTheEditor(
        bool runtimeRunning,
        string _)
    {
        var editorSession = new RecordingEditorSession
        {
            RuntimeRunning = runtimeRunning,
            RejectComponentEditsWith = runtimeRunning ? "node is not in the scene" : null,
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
                                collision: new EngineSceneNodeCollision()),
                        ]))),
            editorSession: editorSession);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var node = Assert.Single(root.Children, n => n.Id == "node");
        viewModel.SceneTree.SelectNode(node);
        Assert.True(viewModel.Inspector.HasCollisionComponent);

        viewModel.Inspector.RemoveCollisionComponentCommand.Execute(null);

        // Either way the engine did not remove it, so the editor must not pretend
        // it did -- the section stays, and the user gets a reason.
        Assert.True(viewModel.Inspector.HasCollisionComponent);
        Assert.NotEmpty(viewModel.Inspector.LastEditError);
        Assert.Contains(node.Components, c => c.Kind == "collision");

        // With the viewport down the engine must not even be asked.
        Assert.Equal(runtimeRunning ? 1 : 0, editorSession.RemovedComponents.Count);
    }

    // D3-C5. The nine transform boxes are plain TextBoxes and TryParseComponent
    // was a bare double.TryParse, so a non-finite value went straight into the
    // live polytree. save_scene then writes the JSON token `null` for it, which
    // read_float3 refuses on load -- and ONE refused component aborts the whole
    // scene parse, so a single keystroke could make the project unopenable.
    //
    // "1e39" is the case that makes this a real bug rather than a hostile-input
    // one: it is a perfectly FINITE double and an ordinary fat-fingered exponent,
    // and it only becomes +inf on the engine's static_cast<float>. A
    // double.IsFinite check would let it through -- the bound has to be float.
    [Theory]
    [InlineData("1e39", false)]
    [InlineData("-1e39", false)]
    [InlineData("NaN", false)]
    [InlineData("Infinity", false)]
    [InlineData("-Infinity", false)]
    [InlineData("1e999", false)]
    [InlineData("1.5", true)]
    [InlineData("-0.5", true)]
    [InlineData("0", true)]
    [InlineData("3.4e38", true)]     // just inside float range: must still work
    public void ALiveTransformComponentIsRejectedUnlessItSurvivesTheFloatCast(
        string text,
        bool expectedAccepted)
    {
        var parse = typeof(WozzitsEngineNativeClient).GetMethod(
            "TryParseLiveTransform",
            System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static);
        Assert.NotNull(parse);

        var edit = new EngineSceneTransformEdit
        {
            TranslationX = text,
            TranslationY = "0",
            TranslationZ = "0",
            RotationX = "0",
            RotationY = "0",
            RotationZ = "0",
            ScaleX = "1",
            ScaleY = "1",
            ScaleZ = "1",
        };

        var args = new object?[] { edit, null };
        var accepted = (bool)parse!.Invoke(null, args)!;

        Assert.Equal(expectedAccepted, accepted);
    }

    // D3-C11. When a node's persisted collision asset id does not resolve to any
    // available option -- the referenced asset-graph node was deleted, or the
    // graph failed to load -- SelectedCollisionOption is null while the label
    // still reads "#7", so the UI asserts the reference exists. The picker is
    // disabled in that state (IsEnabled=HasAvailableCollisionSources) but the
    // "Constrain movement" checkbox is NOT, and toggling it re-pushed id 0 --
    // which the ABI defines as CLEAR. The one control the user could still touch
    // was the one that destroyed the reference.
    [Fact]
    public void TogglingAFlagPreservesAnUnresolvedComponentReference()
    {
        var editorSession = new RecordingEditorSession { RuntimeRunning = true };
        var viewModel = new MainWindowViewModel(
            ProjectSnapshot(
                // No asset-graph nodes at all, so id 7 cannot resolve.
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
                                    CollisionAssetNodeId = 7,
                                    ConstrainMovement = false,
                                }),
                        ]))),
            editorSession: editorSession);

        var root = Assert.Single(viewModel.SceneTree.Nodes);
        var node = Assert.Single(root.Children, n => n.Id == "node");
        viewModel.SceneTree.SelectNode(node);

        // The state the bug needs: unresolved, but displayed as a live reference.
        Assert.Null(viewModel.Inspector.SelectedCollisionOption);
        Assert.Equal("#7", viewModel.Inspector.CollisionReferenceLabel);

        viewModel.Inspector.CollisionConstrainMovement = true;

        var pushed = Assert.Single(editorSession.Collisions);
        Assert.True(pushed.ConstrainMovement);
        // 7, not 0. Pushing 0 here silently destroys the binding the label shows.
        Assert.Equal(7ul, pushed.AssetGraphNodeId);
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

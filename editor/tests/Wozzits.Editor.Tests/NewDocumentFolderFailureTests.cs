using Wozzits.Editor.ViewModels;

namespace Wozzits.Editor.Tests;

/// <summary>
/// D3-P063. Directory.CreateDirectory was the FIRST filesystem call on the New
/// Statechart / New Mind paths -- the guard above it only tests two strings -- and
/// it sat one line ABOVE a try whose catch already named the exact families it
/// throws. With no global handler in the editor, that made a menu click terminate
/// the process and take every dirty document with it.
/// </summary>
public sealed class NewDocumentFolderFailureTests
{
    // A FILE where the folder needs to be. Deterministic on every platform and
    // needs no permission games: CreateDirectory then raises IOException, which is
    // what a removed drive or a read-only share raises in the field.
    private static void WithBlockedBehaviorFolder(Action<string> test)
    {
        var projectDirectory = Path.Combine(
            Path.GetTempPath(),
            "wz-new-doc-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(projectDirectory);
        try
        {
            File.WriteAllText(Path.Combine(projectDirectory, "behavior"), "not a folder");
            test(projectDirectory);
        }
        finally
        {
            Directory.Delete(projectDirectory, recursive: true);
        }
    }

    [Fact]
    public void NewStatechartReportsAFolderItCannotCreate()
    {
        WithBlockedBehaviorFolder(projectDirectory =>
        {
            var viewModel = new MainWindowViewModel(projectDirectory: projectDirectory);

            viewModel.NewStatechartCommand.Execute(null);

            Assert.Contains("Could not create statechart", viewModel.EngineLogText);
        });
    }

    [Fact]
    public void NewMindReportsAFolderItCannotCreate()
    {
        WithBlockedBehaviorFolder(projectDirectory =>
        {
            var viewModel = new MainWindowViewModel(projectDirectory: projectDirectory);

            viewModel.NewMindCommand.Execute(null);

            Assert.Contains("Could not create mind", viewModel.EngineLogText);
        });
    }

    // The control. Both assertions above are about a REPORTED failure, so they
    // would also pass against a command that reported without ever getting near
    // the filesystem -- this pins that the ordinary path still creates the folder
    // and the document in it.
    [Fact]
    public void NewStatechartStillCreatesTheChartWhenTheFolderIsWritable()
    {
        var projectDirectory = Path.Combine(
            Path.GetTempPath(),
            "wz-new-doc-ok-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(projectDirectory);
        try
        {
            var viewModel = new MainWindowViewModel(projectDirectory: projectDirectory);

            viewModel.NewStatechartCommand.Execute(null);

            var charts = Directory.GetFiles(
                Path.Combine(projectDirectory, "behavior", "statecharts"), "*.sc.json");
            Assert.Single(charts);
            Assert.Contains("Created statechart", viewModel.EngineLogText);
        }
        finally
        {
            Directory.Delete(projectDirectory, recursive: true);
        }
    }
}

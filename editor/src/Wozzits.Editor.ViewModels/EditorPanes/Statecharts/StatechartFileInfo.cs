namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// A statechart source file (behavior/statecharts/<name>.sc.json) surfaced in the open menu.
public sealed record StatechartFileInfo(string Name, string Path);

namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// A name/value line in a statechart node's inspector panel (read-only in phase 2).
public sealed record InspectorRow(string Name, string Value);

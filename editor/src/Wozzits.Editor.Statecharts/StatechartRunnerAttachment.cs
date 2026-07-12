namespace Wozzits.Editor.Statecharts;

// The config keys + events the engine's statechart_runner reads (statechart_runner.cpp). The
// editor writes these when attaching a chart to a scene node; the chart_ir is the chart compiled
// as authored (bindings resolve by the node names the chart itself specifies).
public static class StatechartRunnerAttachment
{
    public const string ConfigChart = "chart";
    public const string ConfigChartIr = "chart_ir";
    public static readonly string[] RunnerEvents = { "self.start", "frame.update" };
}

namespace Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

// The minimal surface a node-canvas view-model exposes for the shared interaction
// controller (pan / zoom / marquee-select / node-drag). Both the dataflow and control
// panes implement it, so the pointer/wheel plumbing lives in one place.

public interface ICanvasNode
{
    double X { get; set; }

    double Y { get; set; }

    bool IsSelected { get; set; }
}

public interface IEditorCanvas
{
    double Zoom { get; set; }

    double GraphWidth { get; }

    double GraphHeight { get; }

    double ScaledGraphWidth { get; }

    double ScaledGraphHeight { get; }

    IReadOnlyList<ICanvasNode> CanvasNodes { get; }

    void SelectOnly(ICanvasNode node);

    void ToggleSelection(ICanvasNode node);

    void ClearSelection();

    // Select nodes whose box overlaps the (unordered) rectangle; additive keeps the
    // existing selection (shift-drag).
    void SelectInRectangle(double x0, double y0, double x1, double y1, bool additive);

    void MoveSelectedBy(double dx, double dy);

    // Zoom about the current level by one wheel notch (sign of wheelDelta); the pane clamps.
    void ZoomByWheel(double wheelDelta);
}

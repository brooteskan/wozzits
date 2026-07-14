using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Shapes;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.VisualTree;
using Wozzits.Editor.App.Controls;
using Wozzits.Editor.ViewModels.EditorPanes.Statecharts;

namespace Wozzits.Editor.App.Views.EditorPanes.Statecharts;

// Dataflow canvas. Navigation + selection + node-drag come from the shared GraphInteraction
// controller; the WirePreview line lets it also drive the output->input wiring gesture (M2).
public partial class DataflowPaneView : UserControl
{
    public DataflowPaneView()
    {
        InitializeComponent();
        _ = new GraphInteraction(this, Scroll, Canvas, SelectionRectangle, () => DataContext as IEditorCanvas, WirePreview);
    }

    // Wires and the wiring preview meet the port dots exactly rather than trusting fixed layout
    // constants: once a card is laid out we measure where its output dot and first input dot
    // actually landed (relative to the card's top-left) and hand those offsets to the node. The
    // statechart card's header is shorter than the asset-graph card the constants were tuned for,
    // so without this the wires attach a row too low and short of the dots.
    // Double-click a card: route it to the pane, which opens the referenced mind if the card
    // is a REFERENCE agent (the pane holds the host-window resolver). No-op for other cards.
    private void OnNodeDoubleTapped(object? sender, TappedEventArgs e)
    {
        if ((sender as Control)?.DataContext is DataflowNodeViewModel node
            && DataContext is DataflowPaneViewModel pane)
        {
            pane.ActivateNode(node);
        }
    }

    private void CardLoaded(object? sender, RoutedEventArgs e) => MeasurePorts(sender as Border);

    private void CardSizeChanged(object? sender, SizeChangedEventArgs e) => MeasurePorts(sender as Border);

    private static void MeasurePorts(Border? card)
    {
        if (card?.DataContext is not DataflowNodeViewModel node)
        {
            return;
        }

        foreach (var dot in card.GetVisualDescendants().OfType<Ellipse>())
        {
            if (dot.Bounds.Width <= 0
                || dot.TranslatePoint(new Point(dot.Bounds.Width / 2, dot.Bounds.Height / 2), card) is not { } c)
            {
                continue;
            }

            if (dot.Classes.Contains("AssetGraphOutputPortDot"))
            {
                node.SetMeasuredOutputDot(c.X, c.Y);
            }
            else if (dot.Classes.Contains("AssetGraphInputPortDot")
                     && dot.DataContext is DataflowPortViewModel { Index: 0 })
            {
                node.SetMeasuredInputDot(c.X, c.Y);
            }
        }
    }
}

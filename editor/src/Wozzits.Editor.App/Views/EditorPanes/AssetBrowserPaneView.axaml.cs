using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.App.Views.EditorPanes;

// Shared in-process drag payload between the asset browser (drag source) and the
// asset graph canvas (drop target).
internal static class AssetBrowserDrag
{
    public static readonly DataFormat<AssetBrowserSchemaViewModel> SchemaFormat =
        DataFormat.CreateInProcessFormat<AssetBrowserSchemaViewModel>(
            "wozzits-asset-schema");
}

public partial class AssetBrowserPaneView : UserControl
{
    public AssetBrowserPaneView()
    {
        InitializeComponent();
    }

    private async void SchemaPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (sender is not Control { DataContext: AssetBrowserSchemaViewModel schema })
        {
            return;
        }
        if (!e.GetCurrentPoint(sender as Visual).Properties.IsLeftButtonPressed)
        {
            return;
        }

        var transfer = new DataTransfer();
        transfer.Add(DataTransferItem.Create(AssetBrowserDrag.SchemaFormat, schema));
        await DragDrop.DoDragDropAsync(e, transfer, DragDropEffects.Copy);
    }
}

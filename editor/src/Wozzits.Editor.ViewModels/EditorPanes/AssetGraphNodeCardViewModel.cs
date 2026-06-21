namespace Wozzits.Editor.ViewModels.EditorPanes;

using System.Collections.ObjectModel;
using Wozzits.Editor.Protocol;

public sealed class AssetGraphNodeCardViewModel : ViewModelBase
{
    private double _x;
    private double _y;
    private bool _isSelected;

    public AssetGraphNodeCardViewModel(EngineAssetGraphNode node, double canvasPadding)
    {
        Id = node.Id;
        X = node.X + canvasPadding;
        Y = node.Y + canvasPadding;
        DisplayName = node.DisplayName;
        TypeName = node.TypeName;
        SchemaDisplay = node.Schema;
        CompileStatus = node.CompileStatus;
        InputPorts = new ObservableCollection<AssetGraphPortViewModel>(
            node.InputPorts.Select(port => new AssetGraphPortViewModel(this, port, isInput: true)));
        OutputPorts = new ObservableCollection<AssetGraphPortViewModel>(
            node.OutputPorts.Select(port => new AssetGraphPortViewModel(this, port, isInput: false)));
    }

    public ulong Id { get; }

    public double X
    {
        get => _x;
        set => SetProperty(ref _x, value);
    }

    public double Y
    {
        get => _y;
        set => SetProperty(ref _y, value);
    }

    public bool IsSelected
    {
        get => _isSelected;
        set => SetProperty(ref _isSelected, value);
    }

    public string DisplayName { get; }

    public string TypeName { get; }

    public string SchemaDisplay { get; }

    public string CompileStatus { get; }

    public bool HasInputPorts => InputPorts.Count > 0;

    public ObservableCollection<AssetGraphPortViewModel> InputPorts { get; }

    public ObservableCollection<AssetGraphPortViewModel> OutputPorts { get; }
}

using System;
using System.ComponentModel;
using Avalonia.Controls;
using Avalonia.Threading;
using Wozzits.Editor.ViewModels.EditorPanes;

namespace Wozzits.Editor.App.Views.EditorPanes;

public partial class ConsolePaneView : UserControl
{
    private INotifyPropertyChanged? _observedViewModel;

    public ConsolePaneView()
    {
        InitializeComponent();
        DataContextChanged += OnDataContextChanged;
    }

    private void OnDataContextChanged(object? sender, EventArgs e)
    {
        if (_observedViewModel is not null)
        {
            _observedViewModel.PropertyChanged -= OnViewModelPropertyChanged;
            _observedViewModel = null;
        }

        if (DataContext is INotifyPropertyChanged viewModel)
        {
            _observedViewModel = viewModel;
            _observedViewModel.PropertyChanged += OnViewModelPropertyChanged;
        }
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(ConsolePaneViewModel.LogText))
        {
            return;
        }

        // Stick to the tail only when already at (or near) the bottom, so a user
        // who scrolled up to read scrollback isn't yanked back down by new lines.
        // Checked against the CURRENT extent, before the new text relayouts.
        var scrollViewer = LogScrollViewer;
        var atBottom = scrollViewer.Offset.Y
            >= scrollViewer.Extent.Height - scrollViewer.Viewport.Height - 4.0;
        if (!atBottom)
        {
            return;
        }

        // Defer so the text block has remeasured against the new content before we
        // scroll; otherwise ScrollToEnd uses the stale extent and stops short.
        Dispatcher.UIThread.Post(
            () => scrollViewer.ScrollToEnd(),
            DispatcherPriority.Background);
    }
}

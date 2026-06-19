using System;
using Avalonia.Controls;
using Wozzits.Editor.ViewModels;

namespace Wozzits.Editor.App.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
        Closed += OnClosed;
    }

    private void OnClosed(object? sender, EventArgs e)
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            viewModel.Shutdown();
        }
    }
}

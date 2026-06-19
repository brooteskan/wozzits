using System;
using System.Diagnostics.CodeAnalysis;
using Avalonia.Controls;
using Avalonia.Controls.Templates;
using Wozzits.Editor.ViewModels;

namespace Wozzits.Editor.App;

/// <summary>
/// Given a view model, returns the corresponding view if possible.
/// </summary>
[RequiresUnreferencedCode(
    "Default implementation of ViewLocator involves reflection which may be trimmed away.",
    Url = "https://docs.avaloniaui.net/docs/concepts/view-locator")]
public class ViewLocator : IDataTemplate
{
    public Control? Build(object? param)
    {
        if (param is null)
            return null;
        
        var viewModelName = param.GetType().FullName!;
        var viewNamespaceName = viewModelName.Replace(
            "Wozzits.Editor.ViewModels.",
            "Wozzits.Editor.App.Views.",
            StringComparison.Ordinal);

        var appAssemblyName = typeof(ViewLocator).Assembly.GetName().Name;
        var viewTypeCandidates = new[]
        {
            viewNamespaceName.Replace("ViewModel", "View", StringComparison.Ordinal),
            viewNamespaceName.Replace("ViewModel", string.Empty, StringComparison.Ordinal),
        };

        foreach (var candidate in viewTypeCandidates)
        {
            var type = Type.GetType($"{candidate}, {appAssemblyName}");

            if (type != null)
            {
                return (Control)Activator.CreateInstance(type)!;
            }
        }
        
        return new TextBlock { Text = "Not Found: " + viewNamespaceName };
    }

    public bool Match(object? data)
    {
        return data is ViewModelBase;
    }
}

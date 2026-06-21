namespace Wozzits.Editor.Core.Logging;

public sealed class EditorLogBuffer
{
    private const int MaxBufferedLines = 500;

    private readonly object _gate = new();
    private readonly List<string> _lines = [];
    private Action<string>? _lineReceived;

    public void AppendLine(string line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return;
        }

        Action<string>? handler;
        lock (_gate)
        {
            _lines.Add(line);
            while (_lines.Count > MaxBufferedLines)
            {
                _lines.RemoveAt(0);
            }

            handler = _lineReceived;
        }

        handler?.Invoke(line);
    }

    public IDisposable Subscribe(Action<string> handler)
    {
        List<string> snapshot;
        lock (_gate)
        {
            snapshot = [.. _lines];
            _lineReceived += handler;
        }

        foreach (var line in snapshot)
        {
            handler(line);
        }

        return new Subscription(this, handler);
    }

    private void Unsubscribe(Action<string> handler)
    {
        lock (_gate)
        {
            _lineReceived -= handler;
        }
    }

    private sealed class Subscription : IDisposable
    {
        private readonly EditorLogBuffer _owner;
        private Action<string>? _handler;

        public Subscription(EditorLogBuffer owner, Action<string> handler)
        {
            _owner = owner;
            _handler = handler;
        }

        public void Dispose()
        {
            var handler = _handler;
            if (handler is null)
            {
                return;
            }

            _handler = null;
            _owner.Unsubscribe(handler);
        }
    }
}

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

        // DELIVERED UNDER THE LOCK (D3-P069). The handler used to be captured
        // inside the lock and invoked outside it, so the order lines reach the
        // subscriber could differ from the order they were buffered in. The buffer
        // is fed from at least two threads -- the UI thread, and the engine's
        // LOGGER WORKER via OnNativeLog -- so this is the ordinary case, not a
        // corner: FileLogSink stamps DateTime.Now at Write time, which made the
        // mirrored file's timestamps non-monotonic too, and the file's own comment
        // claims it is "a complete, ordered timeline of a run".
        //
        // The cost is that producers serialise across the handler. That is close to
        // free here, because the handler's real work (FileLogSink.Write) already
        // takes its own lock, so the critical sections nearly coincide -- and the
        // locks are always taken in this order, so there is no cycle to deadlock on.
        lock (_gate)
        {
            _lines.Add(line);
            while (_lines.Count > MaxBufferedLines)
            {
                _lines.RemoveAt(0);
            }

            _lineReceived?.Invoke(line);
        }
    }

    public IDisposable Subscribe(Action<string> handler)
    {
        // Replay under the lock as well, for the same reason: registering the
        // handler and then replaying outside the lock let a line appended by
        // another thread be delivered BEFORE the backlog it came after. In the
        // real startup sequence that is the preamble naming which wozzits_abi.dll
        // was loaded and whether the scene failed to load -- exactly the lines an
        // operator reads to diagnose a bad launch.
        lock (_gate)
        {
            foreach (var line in _lines)
            {
                handler(line);
            }

            _lineReceived += handler;
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

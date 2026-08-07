using System.Collections.Concurrent;
using Wozzits.Editor.Core.Logging;

namespace Wozzits.Editor.Tests;

/// <summary>
/// D3-P069. The buffer is fed from at least two threads — the UI thread and the
/// engine's logger worker (OnNativeLog) — and it used to register a subscriber
/// under the lock but replay the backlog outside it. A line appended during that
/// replay could therefore be delivered BEFORE the backlog it came after.
///
/// The real sequence this breaks is startup: App.CreateProjectWindow appends the
/// preamble, then starts the engine (and its logger worker) while evaluating the
/// session, and only afterwards does the view model Subscribe. So the lines naming
/// which wozzits_abi.dll loaded and whether the scene failed to load are exactly
/// the ones that can arrive out of order.
/// </summary>
public sealed class EditorLogBufferOrderingTests
{
    // ConcurrentQueue rather than List: without the fix two threads deliver
    // concurrently, and the failure this test must report is WRONG ORDER, not a
    // corrupted collection.
    [Fact]
    public void ALineAppendedDuringReplayIsDeliveredAfterTheBacklog()
    {
        const int backlog = 50;
        var buffer = new EditorLogBuffer();
        for (var i = 0; i < backlog; ++i)
        {
            buffer.AppendLine($"backlog {i}");
        }

        var received = new ConcurrentQueue<string>();
        using var replayStarted = new ManualResetEventSlim();

        // The appender waits until the replay is under way, so the window this
        // test aims at is actually open when it appends.
        var appender = Task.Run(() =>
        {
            replayStarted.Wait(TimeSpan.FromSeconds(10));
            buffer.AppendLine("LIVE");
        });

        buffer.Subscribe(line =>
        {
            received.Enqueue(line);
            if (line == "backlog 0")
            {
                replayStarted.Set();
                // Hold the replay open long enough for the appender to get there.
                Thread.Sleep(200);
            }
        });
        appender.Wait(TimeSpan.FromSeconds(10));

        var lines = received.ToArray();
        Assert.Equal(backlog + 1, lines.Length);
        Assert.Equal("LIVE", lines[^1]);
    }

    // The control: ordinary delivery still works, and a subscriber still gets the
    // backlog followed by live lines. Without this, "LIVE came last" would also
    // pass for a buffer that had stopped delivering the backlog at all.
    [Fact]
    public void ASubscriberGetsTheBacklogThenLiveLinesInOrder()
    {
        var buffer = new EditorLogBuffer();
        buffer.AppendLine("first");
        buffer.AppendLine("second");

        var received = new List<string>();
        using var subscription = buffer.Subscribe(received.Add);
        buffer.AppendLine("third");

        Assert.Equal(["first", "second", "third"], received);
    }

    // Unsubscribing still stops delivery — the replay-under-the-lock change moved
    // the += into the same critical section, so this pins that it still happens.
    [Fact]
    public void AnUnsubscribedHandlerStopsReceiving()
    {
        var buffer = new EditorLogBuffer();
        var received = new List<string>();
        var subscription = buffer.Subscribe(received.Add);

        buffer.AppendLine("delivered");
        subscription.Dispose();
        buffer.AppendLine("dropped");

        Assert.Equal(["delivered"], received);
    }
}

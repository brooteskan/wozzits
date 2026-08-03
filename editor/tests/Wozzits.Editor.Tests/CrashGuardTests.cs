using Wozzits.Editor.Core.Logging;
using Xunit;

namespace Wozzits.Editor.Tests;

// D3-Q1 / D3-C7. These cover the PAYLOAD -- what a report says and where it
// goes. They do NOT cover the wiring, and that gap is the honest residual:
//
//   * Dispatcher.UIThread.UnhandledException needs a live Avalonia dispatcher,
//     and this project references no Avalonia headless package;
//   * AppDomain.CurrentDomain.UnhandledException fires only while the process is
//     being torn down, so exercising it would take the test runner with it.
//
// So `args.Handled = true` in App.Initialize -- the line that actually keeps the
// window alive -- is code-reviewed, not test-covered. Said out loud rather than
// left to be discovered, because a green suite here does not mean the editor
// survives anything.
//
// The file FALLBACK is uncovered too, and deliberately: it writes under
// %LOCALAPPDATA%, and a test suite has no business writing there. What that
// leaves untested is "a report survives a broken console", which is the one
// thing the inner catch in Report exists for.
public sealed class CrashGuardTests
{
    [Fact]
    public void ReportReachesTheSink()
    {
        var seen = new List<string>();
        CrashGuard.SetSink(seen.Add);

        CrashGuard.Report("test-origin", new InvalidOperationException("boom"), survived: true);

        Assert.Single(seen);
        Assert.Contains("test-origin", seen[0]);
        Assert.Contains("InvalidOperationException", seen[0]);
        Assert.Contains("boom", seen[0]);
    }

    [Fact]
    public void DescribeSaysWhetherTheEditorSurvived()
    {
        // The distinction a reader needs first: is this window still usable?
        var alive = CrashGuard.Describe("x", new Exception("a"), survived: true);
        var dead = CrashGuard.Describe("x", new Exception("a"), survived: false);

        Assert.Contains("still running", alive);
        Assert.Contains("terminating", dead);
        Assert.DoesNotContain("terminating", alive);
    }

    [Fact]
    public void DescribeUnwrapsTheInnerChain()
    {
        // A Task hands over an AggregateException whose wrapper says nothing; the
        // exception worth reading is always further in.
        var inner = new FormatException("the real problem");
        var outer = new AggregateException("wrapper", inner);

        var text = CrashGuard.Describe("x", outer, survived: true);

        Assert.Contains("AggregateException", text);
        Assert.Contains("FormatException", text);
        Assert.Contains("the real problem", text);
    }

    [Fact]
    public void DescribeBoundsAPathologicallyDeepInnerChain()
    {
        // A CYCLE is not constructible -- InnerException is read-only and set at
        // construction, so the inner must already exist -- but depth is, and a
        // retry loop that wraps its own failure each round produces exactly that.
        // The cap is what keeps one crash report from being the size of the log.
        Exception error = new InvalidOperationException("root");
        for (var i = 0; i < 40; i++)
        {
            error = new InvalidOperationException($"layer {i}", error);
        }

        var text = CrashGuard.Describe("x", error, survived: true);

        Assert.Contains("inner chain truncated", text);
        Assert.DoesNotContain("root", text);   // never reached, and that is the point
    }

    [Fact]
    public void DescribeToleratesAMissingExceptionObject()
    {
        // AppDomain.UnhandledException hands over `object`, and nothing guarantees
        // it is an Exception.
        var text = CrashGuard.Describe("x", error: null, survived: false);

        Assert.Contains("no exception object", text);
    }

    [Fact]
    public void ASinkThatThrowsDoesNotEscapeAndDoesNotRecurse()
    {
        // The sink ends up in the editor console and the file mirror, so it runs
        // arbitrary code. If it throws while we are already handling an exception
        // and the report path is re-entered, the recursion has nothing to stop it.
        var calls = 0;
        CrashGuard.SetSink(_ =>
        {
            calls++;
            throw new InvalidOperationException("the console is broken too");
        });

        // Must not throw, and must not call the sink more than once.
        CrashGuard.Report("test-origin", new Exception("boom"), survived: true);

        Assert.Equal(1, calls);
    }

    [Fact]
    public void InstallSubscribesExactlyOnce()
    {
        // Called from Program.Main; a second call must not double-subscribe, or
        // every future crash reports twice.
        //
        // Asserts on InstallCount, NOT on a Report() round trip: Report goes
        // nowhere near the event subscriptions, so the obvious version of this
        // test passes with the guard removed. That is the placebo shape -- test
        // the mechanism the fix changed, not a neighbouring one that happens to
        // be easier to reach.
        CrashGuard.Install();
        var afterFirst = CrashGuard.InstallCount;
        CrashGuard.Install();
        CrashGuard.Install();

        Assert.Equal(1, afterFirst);
        Assert.Equal(1, CrashGuard.InstallCount);
    }

}

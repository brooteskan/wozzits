using Wozzits.Editor.Core.Behaviors;

namespace Wozzits.Editor.Tests;

/// <summary>
/// D3-P051. The diagnostic collector is fed from BOTH Process output pumps, which
/// .NET raises on two independent threadpool threads with no serialisation between
/// them. It used to be a plain List&lt;string&gt; captured in a closure.
/// </summary>
public sealed class BehaviorDiagnosticCaptureTests
{
    // ROUND COUNT IS LOAD-BEARING FOR THE CAP TEST. Measured with the lock removed:
    // at 1 round the lost-entry test below fails every time (600 unsynchronised Adds
    // lose one reliably), but TheCapHoldsUnderConcurrentOffers PASSES 5 runs out of
    // 5 -- both threads have to read Count == max-1 in the same instant, which is a
    // far narrower window. At 200 rounds both fail. So "simplify this to one round"
    // is exactly the future edit that would silently un-test the cap.
    private const int Rounds = 200;

    [Fact]
    public void ConcurrentOffersFromBothPumpsKeepEveryDiagnostic()
    {
        const int perPump = 300;

        for (var round = 0; round < Rounds; ++round)
        {
            // Cap above the total, so this measures the collector rather than the cap.
            var capture = new BehaviorDiagnosticCapture(max: perPump * 2 + 1);

            // Unique payload per pump, so a lost or duplicated entry is visible as a
            // count, not merely suspected.
            var stdout = Task.Run(() =>
            {
                for (var i = 0; i < perPump; ++i)
                {
                    capture.Offer($"ninja.cpp({i}): error C2440: from stdout");
                }
            });
            var stderr = Task.Run(() =>
            {
                for (var i = 0; i < perPump; ++i)
                {
                    capture.Offer($"cmake.cpp:{i}:1: error: from stderr");
                }
            });
            Task.WaitAll(stdout, stderr);

            var lines = capture.Snapshot();
            Assert.Equal(perPump * 2, lines.Count);
            Assert.Equal(perPump * 2, lines.Distinct().Count());
        }
    }

    // The cap has to hold under the same concurrency: read the count outside the
    // lock and two threads both see max-1, so both append and the UI gets a block
    // longer than the one the cap exists to prevent.
    [Fact]
    public void TheCapHoldsUnderConcurrentOffers()
    {
        const int max = 40;

        for (var round = 0; round < Rounds; ++round)
        {
            var capture = new BehaviorDiagnosticCapture(max);

            Task.WaitAll(
                Task.Run(() =>
                {
                    for (var i = 0; i < 200; ++i)
                    {
                        capture.Offer($"a.cpp({i}): error C2440: x");
                    }
                }),
                Task.Run(() =>
                {
                    for (var i = 0; i < 200; ++i)
                    {
                        capture.Offer($"b.cpp:{i}:1: error: y");
                    }
                }));

            Assert.Equal(max, capture.Snapshot().Count);
        }
    }

    // The control: the collector still does its actual job -- keep diagnostics,
    // drop ordinary build chatter -- so the two assertions above are not just
    // measuring an empty list.
    [Fact]
    public void OnlyDiagnosticLinesAreCaptured()
    {
        var capture = new BehaviorDiagnosticCapture(max: 40);

        capture.Offer("[1/8] Building CXX object foo.cpp.obj");
        capture.Offer("foo.cpp(25,13): error C2440: cannot convert");
        capture.Offer("ninja: build stopped: subcommand failed.");
        capture.Offer("CMake Error at CMakeLists.txt:5 (add_library):");

        Assert.Equal(
            [
                "foo.cpp(25,13): error C2440: cannot convert",
                "CMake Error at CMakeLists.txt:5 (add_library):",
            ],
            capture.Snapshot());
    }
}

using System.IO;
using Wozzits.Editor.Core.Behaviors;

namespace Wozzits.Editor.Tests;

public sealed class BehaviorModuleBuilderTests
{
    [Fact]
    public async Task RebuildSkipsWhenProjectHasNoBehaviorCMakeLists()
    {
        var projectDir = Path.Combine(
            Path.GetTempPath(),
            "wz-behavior-build-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(projectDir);
        try
        {
            var log = new List<string>();
            var builder = new BehaviorModuleBuilder();

            var result = await builder.RebuildAsync(projectDir, log.Add);

            // No behavior/CMakeLists.txt -> nothing to build (not a failure), and
            // CMake is never invoked, so the test needs no toolchain.
            Assert.Equal(BehaviorBuildOutcome.Skipped, result.Outcome);
            Assert.Empty(result.Errors);
            Assert.Contains(log, line => line.Contains("nothing to build"));
        }
        finally
        {
            Directory.Delete(projectDir, recursive: true);
        }
    }

    [Fact]
    public async Task RebuildSkipsWhenProjectDirectoryIsEmpty()
    {
        var log = new List<string>();
        var builder = new BehaviorModuleBuilder();

        var result = await builder.RebuildAsync(string.Empty, log.Add);

        Assert.Equal(BehaviorBuildOutcome.Skipped, result.Outcome);
        Assert.Empty(result.Errors);
    }

    // A failed rebuild used to leave nothing behind but a line in a busy console, so the
    // stale DLL looked like a broken editor. These pin WHICH lines get captured for the UI.
    [Theory]
    [InlineData("D:/p/enemy_tank_v1/enemy_tank_v1.cpp:25:13: error: constant expression evaluates to -1", true)]
    [InlineData("D:/p/foo.cpp(25,13): error C2440: cannot convert", true)]
    [InlineData("D:/p/foo.cpp:9:1: fatal error: 'bar.h' file not found", true)]
    [InlineData("CMake Error at CMakeLists.txt:5 (add_library):", true)]
    // Noise: ninja's summaries and our own step line are not diagnostics -- the real
    // error is already captured, and these would just pad the block.
    [InlineData("FAILED: [code=1] CMakeFiles/enemy_tank_v1.dir/enemy_tank_v1.cpp.obj", false)]
    [InlineData("ninja: build stopped: subcommand failed.", false)]
    [InlineData("[behavior/build] cmake --build --preset clang-debug failed (exit 1).", false)]
    [InlineData("4 errors generated.", false)]
    [InlineData("[1/2] Building CXX object CMakeFiles/enemy_tank_v1.dir/enemy_tank_v1.cpp.obj", false)]
    public void DiagnosticLinesAreTheCompilerErrorsNotTheNoise(string line, bool isDiagnostic)
    {
        Assert.Equal(isDiagnostic, BehaviorModuleBuilder.IsDiagnosticLine(line));
    }

    [Fact]
    public void CacheCopiedFromAnotherProjectIsForeign()
    {
        WithTempBehaviorDir((behaviorDir, configureDir) =>
        {
            // A cache that records a DIFFERENT source/binary dir (folder copied
            // from another project) is foreign.
            WriteCache(
                configureDir,
                home: "D:/code/wozzits/wozzits-window-engine/.../terrain_collision/behavior",
                cacheFileDir: "D:/code/wozzits/wozzits-window-engine/.../terrain_collision/behavior/build/cmake-clang-debug");

            Assert.True(
                BehaviorModuleBuilder.IsConfigureCacheForeign(behaviorDir, configureDir));
        });
    }

    [Fact]
    public void CacheGeneratedInPlaceIsNotForeign()
    {
        WithTempBehaviorDir((behaviorDir, configureDir) =>
        {
            // A cache recording this folder's own dirs (forward slashes + mixed
            // drive-letter case, as CMake writes them) is reused, not wiped.
            WriteCache(
                configureDir,
                home: behaviorDir.Replace('\\', '/'),
                cacheFileDir: configureDir.Replace('\\', '/').ToLowerInvariant());

            Assert.False(
                BehaviorModuleBuilder.IsConfigureCacheForeign(behaviorDir, configureDir));
        });
    }

    [Fact]
    public void NoCacheIsNotForeign()
    {
        WithTempBehaviorDir((behaviorDir, configureDir) =>
            Assert.False(
                BehaviorModuleBuilder.IsConfigureCacheForeign(behaviorDir, configureDir)));
    }

    private static void WithTempBehaviorDir(Action<string, string> test)
    {
        var behaviorDir = Path.Combine(
            Path.GetTempPath(),
            "wz-cache-foreign-" + Guid.NewGuid().ToString("N"));
        var configureDir = Path.Combine(behaviorDir, "build", "cmake-clang-debug");
        Directory.CreateDirectory(configureDir);
        try
        {
            test(behaviorDir, configureDir);
        }
        finally
        {
            Directory.Delete(behaviorDir, recursive: true);
        }
    }

    private static void WriteCache(string configureDir, string home, string cacheFileDir)
    {
        File.WriteAllText(
            Path.Combine(configureDir, "CMakeCache.txt"),
            $"CMAKE_CACHEFILE_DIR:INTERNAL={cacheFileDir}\n"
            + $"CMAKE_HOME_DIRECTORY:INTERNAL={home}\n");
    }
}

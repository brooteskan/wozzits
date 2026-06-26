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

            var outcome = await builder.RebuildAsync(projectDir, log.Add);

            // No behavior/CMakeLists.txt -> nothing to build (not a failure), and
            // CMake is never invoked, so the test needs no toolchain.
            Assert.Equal(BehaviorBuildOutcome.Skipped, outcome);
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

        var outcome = await builder.RebuildAsync(string.Empty, log.Add);

        Assert.Equal(BehaviorBuildOutcome.Skipped, outcome);
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

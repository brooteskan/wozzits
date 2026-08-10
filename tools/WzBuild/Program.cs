using System.Diagnostics;

namespace WzBuild;

// wzbuild — a small orchestrator over the two toolchains (the s&box SboxBuild
// analog, right-sized). It builds the C++ engine with CMake and publishes the
// .NET editor, both into one install folder. It does NOT replace the CMake
// presets or `dotnet` — it drives them.
internal static class Program
{
    static int Main(string[] args)
    {
        try
        {
            if (args.Length == 0) { Usage(); return 1; }

            string command = args[0].ToLowerInvariant();
            if (command is "-h" or "--help" or "help") { Usage(); return 0; }

            var opts = Options.Parse(args[1..]);
            var cfg = Config.Resolve(opts);

            Log.Info($"install={cfg.InstallDir}  config={cfg.BuildConfig}  publish={cfg.EditorPublish}");
            if (opts.DryRun) Log.Info("(dry run — commands are printed, not executed)");

            bool ok = command switch
            {
                "build" => Steps.Build(cfg, opts),
                "test" => Steps.Test(cfg, opts),
                _ => UnknownCommand(command),
            };
            return ok ? 0 : 1;
        }
        catch (Exception ex)
        {
            Log.Error(ex.Message);
            return 1;
        }
    }

    static bool UnknownCommand(string command)
    {
        Log.Error($"unknown command '{command}'");
        Usage();
        return false;
    }

    static void Usage()
    {
        Console.WriteLine(
            "wzbuild - build the wozzits app (engine + editor)\n\n" +
            "Usage:\n" +
            "  build.cmd <command> [options]\n\n" +
            "Commands:\n" +
            "  build   Build+install the engine and publish the editor into the install dir\n" +
            "  test    Run engine (ctest) and editor (dotnet test) tests\n\n" +
            "Options:\n" +
            "  --config <Debug|Release>       build configuration (default Debug)\n" +
            "  --install-dir <path>           app install/publish folder\n" +
            "  --publish <framework-dependent|self-contained>   editor publish mode\n" +
            "  --clean                        reconfigure the engine from scratch\n" +
            "  --skip-native                  skip the CMake engine half\n" +
            "  --skip-managed                 skip the .NET editor half\n" +
            "  --dry-run                      print commands without running them\n\n" +
            "Options (test only):\n" +
            "  --label <ctest label>          run only one test category (e.g. tasks)\n" +
            "  --repeat <N>                   re-run until failure, up to N times\n\n" +
            "Race hunting (a timing bug is green on any single Debug run):\n" +
            "  build.cmd test --config Release --label tasks --repeat 20\n\n" +
            "Variables (precedence: flag > environment > build.env > default):\n" +
            "  WOZZITS_APP_INSTALL_DIR, WOZZITS_BUILD_CONFIG, WOZZITS_EDITOR_PUBLISH");
    }
}

internal sealed class Options
{
    public string? Config;
    public string? InstallDir;
    public string? Publish;
    public bool Clean;
    public bool SkipNative;
    public bool SkipManaged;
    public bool DryRun;
    public string? Label;
    public int Repeat;

    public static Options Parse(string[] args)
    {
        var o = new Options();
        for (int i = 0; i < args.Length; i++)
        {
            string a = args[i];
            switch (a)
            {
                case "--config": o.Config = Next(args, ref i, a); break;
                case "--install-dir": o.InstallDir = Next(args, ref i, a); break;
                case "--publish": o.Publish = Next(args, ref i, a); break;
                case "--clean": o.Clean = true; break;
                case "--skip-native": o.SkipNative = true; break;
                case "--skip-managed": o.SkipManaged = true; break;
                case "--dry-run": o.DryRun = true; break;
                case "--label": o.Label = Next(args, ref i, a); break;
                case "--repeat": o.Repeat = ParseRepeat(Next(args, ref i, a)); break;
                default: throw new ArgumentException($"unknown option '{a}'");
            }
        }
        return o;
    }

    static int ParseRepeat(string value)
    {
        if (!int.TryParse(value, out int n) || n < 1)
            throw new ArgumentException($"--repeat needs a positive integer (got '{value}')");
        return n;
    }

    static string Next(string[] args, ref int i, string flag)
    {
        if (i + 1 >= args.Length) throw new ArgumentException($"{flag} needs a value");
        return args[++i];
    }
}

internal sealed class Config
{
    public required string RepoRoot;
    public required string InstallDir;
    public required string BuildConfig;    // Debug | Release
    public required string EditorPublish;  // framework-dependent | self-contained

    public string Preset => BuildConfig == "Release" ? "clang-release" : "clang-debug";
    public string BuildDir => $"build/{Preset}";

    public static Config Resolve(Options o)
    {
        string root = FindRepoRoot();
        var env = DotEnv.Load(Path.Combine(root, "build.env"));

        static string? NonEmpty(string? s) => string.IsNullOrWhiteSpace(s) ? null : s;
        string Pick(string? cli, string name, string fallback)
            => NonEmpty(cli)
               ?? NonEmpty(Environment.GetEnvironmentVariable(name))
               ?? NonEmpty(env.GetValueOrDefault(name))
               ?? fallback;

        string installDir = Pick(o.InstallDir, "WOZZITS_APP_INSTALL_DIR", "D:/wozzits-app");
        string config = Normalize(Pick(o.Config, "WOZZITS_BUILD_CONFIG", "Debug"),
                                  ["Debug", "Release"], "config");
        string publish = Normalize(Pick(o.Publish, "WOZZITS_EDITOR_PUBLISH", "framework-dependent"),
                                   ["framework-dependent", "self-contained"], "publish");

        return new Config
        {
            RepoRoot = root,
            InstallDir = installDir,
            BuildConfig = config,
            EditorPublish = publish,
        };
    }

    static string Normalize(string value, string[] allowed, string what)
    {
        foreach (var a in allowed)
            if (string.Equals(value, a, StringComparison.OrdinalIgnoreCase))
                return a;
        throw new ArgumentException($"{what} must be one of [{string.Join(", ", allowed)}] (got '{value}')");
    }

    // Walk up from the tool's location to the repo root (the dir with CMakePresets.json),
    // so paths are correct regardless of the caller's working directory.
    static string FindRepoRoot()
    {
        for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
            if (File.Exists(Path.Combine(dir.FullName, "CMakePresets.json")))
                return dir.FullName;
        return Directory.GetCurrentDirectory();
    }
}

internal static class DotEnv
{
    // Minimal KEY=value reader (comments with '#', blank lines ignored).
    public static Dictionary<string, string> Load(string path)
    {
        var dict = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (!File.Exists(path)) return dict;
        foreach (var raw in File.ReadAllLines(path))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith('#')) continue;
            int eq = line.IndexOf('=');
            if (eq <= 0) continue;
            string key = line[..eq].Trim();
            string val = line[(eq + 1)..].Trim().Trim('"');
            if (key.Length > 0) dict[key] = val;
        }
        return dict;
    }
}

internal static class Steps
{
    public static bool Build(Config cfg, Options o)
    {
        if (!o.SkipNative)
        {
            if (o.Clean)
            {
                string dir = Path.Combine(cfg.RepoRoot, cfg.BuildDir);
                if (o.DryRun) Log.DryRun($"delete {cfg.BuildDir}");
                else if (Directory.Exists(dir)) { Log.Cmd($"delete {cfg.BuildDir}"); Directory.Delete(dir, true); }
            }

            Log.Info("Configure engine");
            if (!Proc.Run(cfg, o.DryRun, "cmake", "--preset", cfg.Preset,
                          "-D", $"WOZZITS_APP_INSTALL_DIR={cfg.InstallDir}"))
                return false;

            Log.Info("Build engine binaries");
            if (!Proc.Run(cfg, o.DryRun, "cmake", "--build", "--preset", cfg.Preset,
                          "--target", "wozzits_abi", "wozzits_editor_host", "wozzits_app_v1"))
                return false;

            Log.Info("Install app binaries");
            if (!Proc.Run(cfg, o.DryRun, "cmake", "--install", cfg.BuildDir, "--component", "app"))
                return false;
        }

        if (!o.SkipManaged)
        {
            Log.Info("Publish editor");
            var args = new List<string>
            {
                "publish", "editor/src/Wozzits.Editor.App",
                "-c", cfg.BuildConfig,
                $"-p:WozzitsAppInstallDir={cfg.InstallDir}",
            };
            if (cfg.EditorPublish == "self-contained")
                args.AddRange(["-r", "win-x64", "--self-contained", "true"]);
            else
                args.AddRange(["--self-contained", "false"]);

            if (!Proc.Run(cfg, o.DryRun, "dotnet", args.ToArray()))
                return false;
        }

        Log.Ok($"app assembled in {cfg.InstallDir}");
        return true;
    }

    public static bool Test(Config cfg, Options o)
    {
        if (!o.SkipNative)
        {
            Log.Info($"Engine tests (ctest, {cfg.BuildConfig})");
            var args = new List<string> { "--preset", cfg.Preset };

            // Narrow to one ctest label, e.g. `--label tasks`. Pairs with --repeat:
            // hunting a race is worth many runs of the concurrency suites and no
            // runs of the 300 that cannot race.
            if (!string.IsNullOrWhiteSpace(o.Label))
            {
                args.Add("-L");
                args.Add(o.Label);
            }

            // Race hunting. A timing bug that reproduces one run in seven is green
            // on any single run -- both threading crashes found so far (#293's
            // stack-Counter use-after-free, the release-only fibre SIGSEGV) were
            // that shape. until-fail stops at the first failure and keeps its
            // output, so the run that reproduced is the one you read.
            if (o.Repeat > 1)
            {
                args.Add("--repeat");
                args.Add($"until-fail:{o.Repeat}");
            }

            if (!Proc.Run(cfg, o.DryRun, "ctest", args.ToArray()))
                return false;
        }

        if (!o.SkipManaged)
        {
            Log.Info("Editor tests (dotnet test)");
            if (!Proc.Run(cfg, o.DryRun, "dotnet", "test",
                          "editor/tests/Wozzits.Editor.Tests/Wozzits.Editor.Tests.csproj",
                          "-c", cfg.BuildConfig))
                return false;
        }

        Log.Ok("tests passed");
        return true;
    }
}

internal static class Proc
{
    public static bool Run(Config cfg, bool dryRun, string exe, params string[] args)
    {
        string display = exe + " " + string.Join(' ', args.Select(Quote));
        if (dryRun) { Log.DryRun(display); return true; }

        Log.Cmd(display);
        var psi = new ProcessStartInfo(exe) { WorkingDirectory = cfg.RepoRoot, UseShellExecute = false };
        foreach (var a in args) psi.ArgumentList.Add(a);

        using var p = Process.Start(psi);
        if (p is null) { Log.Error($"could not start '{exe}' (is it on PATH?)"); return false; }
        p.WaitForExit();
        if (p.ExitCode != 0) { Log.Error($"{exe} exited with code {p.ExitCode}"); return false; }
        return true;
    }

    static string Quote(string s) => s.Contains(' ') ? $"\"{s}\"" : s;
}

internal static class Log
{
    public static void Info(string m) => Write(ConsoleColor.Cyan, "==>", m);
    public static void Ok(string m) => Write(ConsoleColor.Green, " OK", m);
    public static void Cmd(string m) => Write(ConsoleColor.DarkGray, "  $", m);
    public static void DryRun(string m) => Write(ConsoleColor.Yellow, "  ·", m);
    public static void Error(string m) => Write(ConsoleColor.Red, "ERR", m);

    static void Write(ConsoleColor color, string tag, string message)
    {
        var prev = Console.ForegroundColor;
        Console.ForegroundColor = color;
        Console.Write(tag + " ");
        Console.ForegroundColor = prev;
        Console.WriteLine(message);
    }
}

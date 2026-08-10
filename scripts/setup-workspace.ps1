<#
.SYNOPSIS
    One-time setup for a fresh wozzits clone.

.DESCRIPTION
    Prepares a freshly cloned wozzits repository for building:
        1. Initialises the git submodule the build needs (external/pmp).
        2. Installs the local pre-push hook (build + test gate; see BUILDING.md).

    The repository is otherwise self-contained — the renderer (rhi/), scene layer
    (src/scene_render), and math library (src/algo_math) are vendored in-tree, so
    no sibling repositories need to be cloned.

.PARAMETER SkipSubmodules
    Skip initialising the external/pmp submodule.

.PARAMETER SkipHook
    Skip installing the pre-push git hook.

.EXAMPLE
    .\scripts\setup-workspace.ps1

.EXAMPLE
    .\scripts\setup-workspace.ps1 -SkipHook
#>

[CmdletBinding()]
param(
    [switch] $SkipSubmodules,
    [switch] $SkipHook
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Helpers ───────────────────────────────────────────────────────────────────

function Write-Step  { param([string]$Msg) Write-Host "==> $Msg" -ForegroundColor Cyan }
function Write-Ok    { param([string]$Msg) Write-Host "    OK  $Msg" -ForegroundColor Green }
function Write-Skip  { param([string]$Msg) Write-Host "    --  $Msg" -ForegroundColor DarkGray }
function Write-Fail  { param([string]$Msg) Write-Host "    ERR $Msg" -ForegroundColor Red }

# ── Main ──────────────────────────────────────────────────────────────────────

$repoRoot = Split-Path -Parent $PSScriptRoot          # …/wozzits

Write-Step "Repository root: $repoRoot"

# Build-time submodule: external/pmp (mesh processing). Scoped on purpose so a
# fresh clone pulls only what the build needs.
if (-not $SkipSubmodules) {
    Write-Step "Initialising submodule external/pmp"
    git -C $repoRoot submodule update --init external/pmp
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "git submodule update failed"
        throw "Submodule init failed"
    }
    Write-Ok "external/pmp ready"
} else {
    Write-Skip "Skipped submodule init (-SkipSubmodules)"
}

# Local pre-push CI: build + test on this machine before every push (no cloud
# compute). The hook is version-controlled in .githooks; point git at it. Bypass
# a single push with 'git push --no-verify'. See BUILDING.md.
if (-not $SkipHook) {
    git -C $repoRoot config core.hooksPath .githooks
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "Failed to configure core.hooksPath"
        throw "Hook setup failed"
    }
    Write-Ok "Configured local pre-push CI (git core.hooksPath -> .githooks)"
} else {
    Write-Skip "Skipped pre-push hook install (-SkipHook)"
}

Write-Host ""
Write-Host "Setup complete." -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Open a VS 2022 x64 Developer Command Prompt with clang-cl/lld-link on PATH"
Write-Host "  2. cd $repoRoot"
Write-Host "  3. cmake --preset clang-debug"
Write-Host "  4. cmake --build --preset clang-debug"
Write-Host "  5. ctest --preset clang-debug"

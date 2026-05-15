<#
.SYNOPSIS
    Sets up the Wozzits workspace by cloning required sibling repositories.

.DESCRIPTION
    Clones any missing sibling repositories into the parent directory of
    wozzits-window-engine so that the CMake build can find them.

    Expected workspace layout after running:
        wozzits/
          wozzits-window-engine/   (this repo)
          wozzits-scene-render/    (cloned if missing)

.PARAMETER BaseUrl
    GitHub base URL to clone from. Default: https://github.com/woguls

.PARAMETER SkipSceneRender
    Skip cloning wozzits-scene-render.

.EXAMPLE
    .\scripts\setup-workspace.ps1

.EXAMPLE
    .\scripts\setup-workspace.ps1 -BaseUrl https://github.com/my-fork
#>

[CmdletBinding()]
param(
    [string] $BaseUrl       = "https://github.com/woguls",
    [switch] $SkipSceneRender
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Helpers ───────────────────────────────────────────────────────────────────

function Write-Step  { param([string]$Msg) Write-Host "==> $Msg" -ForegroundColor Cyan }
function Write-Ok    { param([string]$Msg) Write-Host "    OK  $Msg" -ForegroundColor Green }
function Write-Skip  { param([string]$Msg) Write-Host "    --  $Msg" -ForegroundColor DarkGray }
function Write-Fail  { param([string]$Msg) Write-Host "    ERR $Msg" -ForegroundColor Red }

function Ensure-Repo {
    param(
        [string] $Name,
        [string] $TargetPath
    )

    if (Test-Path (Join-Path $TargetPath "CMakeLists.txt")) {
        Write-Ok "$Name already present at $TargetPath"
        return
    }

    if (Test-Path $TargetPath) {
        Write-Fail "$TargetPath exists but has no CMakeLists.txt — unexpected contents."
        throw "Unexpected directory at $TargetPath"
    }

    $url = "$BaseUrl/$Name.git"
    Write-Step "Cloning $Name from $url"
    git clone $url $TargetPath
    if ($LASTEXITCODE -ne 0) {
        Write-Fail "git clone failed for $Name"
        throw "Clone failed"
    }
    Write-Ok "Cloned $Name"
}

# ── Main ──────────────────────────────────────────────────────────────────────

$repoRoot   = Split-Path -Parent $PSScriptRoot          # …/wozzits-window-engine
$workspaceRoot = Split-Path -Parent $repoRoot           # …/wozzits

Write-Step "Workspace root: $workspaceRoot"

if (-not $SkipSceneRender) {
    Ensure-Repo -Name "wozzits-scene-render" `
                -TargetPath (Join-Path $workspaceRoot "wozzits-scene-render")
}

Write-Host ""
Write-Host "Workspace ready." -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Open a VS 2022 x64 Developer Command Prompt"
Write-Host "  2. cd $repoRoot"
Write-Host "  3. cmake --preset windows-debug"
Write-Host "  4. cmake --build --preset windows-debug"
Write-Host "  5. ctest --preset windows-debug"

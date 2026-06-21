[CmdletBinding()]
param(
    [string]$GodotExe = $env:SNT_GODOT_EXE,
    [string]$BuildDir = "build-vs",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$CaptureBaseline,
    [ValidateRange(5, 600)]
    [int]$BaselineSeconds = 15
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$ResolvedBuildDir = Join-Path $ProjectRoot $BuildDir

# Codex Desktop can expose both Path and PATH in the native environment.
# MSBuild treats them as duplicate case-insensitive keys when spawning CL.exe.
$CleanPath = $env:PATH
Remove-Item Env:Path -ErrorAction SilentlyContinue
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $CleanPath

function Resolve-GodotExecutable {
    param([string]$RequestedPath)

    if ($RequestedPath -and (Test-Path -LiteralPath $RequestedPath -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $Command = Get-Command godot -ErrorAction SilentlyContinue
    if ($Command) {
        return $Command.Source
    }

    $KnownPath = "D:\Godot_v4.6.3-stable_win64.exe\Godot_v4.6.3-stable_win64_console.exe"
    if (Test-Path -LiteralPath $KnownPath -PathType Leaf) {
        return $KnownPath
    }

    throw "Godot executable not found. Pass -GodotExe or set SNT_GODOT_EXE."
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)]
        [string]$Label,
        [Parameter(Mandatory)]
        [scriptblock]$Command
    )

    Write-Host "[U0] $Label"
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE"
    }
}

function Invoke-GodotChecked {
    param(
        [Parameter(Mandatory)]
        [string]$Label,
        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    Write-Host "[U0] $Label"
    $Output = & $Godot @Arguments 2>&1
    $ExitCode = $LASTEXITCODE
    $Output | ForEach-Object { Write-Host $_ }
    $FatalPattern = "SCRIPT ERROR|Parse Error|Failed to load script|SHADER ERROR|" +
        "Shader compilation failed|String formatting error|U0 .* failed"
    if ($ExitCode -ne 0 -or ($Output -match $FatalPattern)) {
        throw "$Label failed with exit code $ExitCode"
    }
}

$Godot = Resolve-GodotExecutable $GodotExe

Push-Location $ProjectRoot
try {
    Invoke-Checked "Configure CMake tests" {
        cmake -S src -B $ResolvedBuildDir -DBUILD_TESTING=ON
    }
    Invoke-Checked "Build C++ core smoke" {
        cmake --build $ResolvedBuildDir --config $Configuration `
            --target snt_u0_world_data_smoke
    }
    Invoke-Checked "Build Debug GDExtension for Godot tests" {
        cmake --build $ResolvedBuildDir --config Debug `
            --target science_and_theology
    }
    Invoke-Checked "Run C++ tests" {
        ctest --test-dir $ResolvedBuildDir -C $Configuration --output-on-failure
    }

    $GDScriptTests = @(
        "res://scripts/test/test_item_key_lookup.gd",
        "res://scripts/test/test_content_database.gd",
        "res://scripts/test/test_furnace_command_server.gd",
        "res://scripts/test/test_player_helper.gd",
        "res://scripts/test/test_planet_build_frame.gd",
        "res://scripts/test/test_chunk_mesh_helper.gd"
    )
    foreach ($TestScript in $GDScriptTests) {
        Invoke-GodotChecked "Run $TestScript" @(
            "--headless", "--path", $ProjectRoot, "--script", $TestScript)
    }

    Invoke-GodotChecked "Run U0 world scene smoke" @(
        "--headless", "--path", $ProjectRoot,
        "res://scripts/test/U0WorldSceneSmoke.tscn")

    Invoke-GodotChecked "Run main menu startup smoke" @(
        "--headless", "--path", $ProjectRoot,
        "res://scripts/test/MainMenuStartupSmoke.tscn")

    Invoke-GodotChecked "Validate Godot project startup" @(
        "--headless", "--path", $ProjectRoot, "--editor", "--quit")

    if ($CaptureBaseline) {
        $BaselinePath = Join-Path $ResolvedBuildDir "u0-prototype-baseline.json"
        $PreviousBaseline = $env:SNT_U0_BASELINE
        $PreviousSeconds = $env:SNT_U0_BASELINE_SECONDS
        $PreviousOutput = $env:SNT_U0_BASELINE_OUTPUT
        $PreviousQuit = $env:SNT_U0_BASELINE_QUIT
        $PreviousSeed = $env:SNT_U0_BASELINE_SEED
        try {
            $env:SNT_U0_BASELINE = "1"
            $env:SNT_U0_BASELINE_SECONDS = [string]$BaselineSeconds
            $env:SNT_U0_BASELINE_OUTPUT = $BaselinePath
            $env:SNT_U0_BASELINE_QUIT = "1"
            $env:SNT_U0_BASELINE_SEED = "20260619"
            Invoke-GodotChecked "Capture $BaselineSeconds second U0 baseline" @(
                "--headless", "--path", $ProjectRoot, "res://WorldMap.tscn")
        }
        finally {
            $env:SNT_U0_BASELINE = $PreviousBaseline
            $env:SNT_U0_BASELINE_SECONDS = $PreviousSeconds
            $env:SNT_U0_BASELINE_OUTPUT = $PreviousOutput
            $env:SNT_U0_BASELINE_QUIT = $PreviousQuit
            $env:SNT_U0_BASELINE_SEED = $PreviousSeed
        }
        Write-Host "[U0] Baseline written to $BaselinePath"
    }

    Write-Host "[U0] All checks passed."
}
finally {
    Pop-Location
}

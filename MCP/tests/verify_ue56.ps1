<#
.SYNOPSIS
  One-command verification of the editor + CortexAnimation additions on a target
  engine (intended for UE 5.6). Builds the editor target, ensures a live editor,
  runs the e2e self-test, writes a results report, and (by default) commits +
  pushes that report so it syncs back via git.

.PARAMETER Engine
  Engine root (folder containing Engine\Build\BatchFiles\Build.bat). Defaults to $env:UE_ROOT.
.PARAMETER Project
  Absolute path to the host .uproject that has this plugin under Plugins\.
.PARAMETER LaunchEditor
  Launch the editor and wait for CortexCore. Omit if you'll open it yourself (script still waits).
.PARAMETER SkipBuild
  Skip the compile step.
.PARAMETER NoPush
  Write the report but do not git commit/push it.
.PARAMETER SafeDDC
  Launch with -ddc=InstalledNoZenLocalFallback (avoids a ZenServer DDC hang).

.EXAMPLE
  ./verify_ue56.ps1 -Engine C:\EpicGames\UE_5.6 -Project C:\dev\MyProj\MyProj.uproject -LaunchEditor
#>
param(
  [string]$Engine = $env:UE_ROOT,
  [Parameter(Mandatory = $true)][string]$Project,
  [switch]$LaunchEditor,
  [switch]$SkipBuild,
  [switch]$NoPush,
  [switch]$SafeDDC
)

$ErrorActionPreference = "Stop"
$MCPDir     = Split-Path -Parent $PSScriptRoot
$ResultsDir = Join-Path $PSScriptRoot "verify-results"
New-Item -ItemType Directory -Force $ResultsDir | Out-Null

if (-not $Engine) { throw "No engine path. Pass -Engine or set UE_ROOT." }
if (-not (Test-Path $Project)) { throw "Project not found: $Project" }
$BuildBat = Join-Path $Engine "Engine\Build\BatchFiles\Build.bat"
if (-not (Test-Path $BuildBat)) { throw "Build.bat not found: $BuildBat" }

$ProjectDir   = Split-Path -Parent $Project
$ProjectName  = [IO.Path]::GetFileNameWithoutExtension($Project)
$EditorTarget = $ProjectName + "Editor"
$SavedDir     = Join-Path $ProjectDir "Saved"
$Stamp        = Get-Date -Format "yyyyMMdd-HHmmss"
$ResultFile   = Join-Path $ResultsDir ("UE-" + $env:COMPUTERNAME + "-" + $Stamp + ".md")

$EngineVersion = "unknown"
$verFile = Join-Path $Engine "Engine\Build\Build.version"
if (Test-Path $verFile) {
  try {
    $v = Get-Content $verFile -Raw | ConvertFrom-Json
    $EngineVersion = "$($v.MajorVersion).$($v.MinorVersion).$($v.PatchVersion)"
  } catch {}
}

$script:lines = @()
function Log($m) { Write-Host $m; $script:lines += $m }

Log "# Cortex gems verification - $Stamp"
Log ""
Log "- Host: $($env:COMPUTERNAME)"
Log "- Engine: $Engine (version $EngineVersion)"
Log "- Project: $Project"
Log ""

function Save-And-Exit($code) {
  ($script:lines -join "`r`n") | Out-File -FilePath $ResultFile -Encoding utf8
  Write-Host ""
  Write-Host "Results report: $ResultFile"
  if (-not $NoPush) { & (Join-Path $PSScriptRoot "_sync_results.ps1") -Stamp $Stamp }
  else { Write-Host "(-NoPush) Sync later with: git add MCP/tests/verify-results; git commit -m verify; git push" }
  exit $code
}

# Phase 1: build
if (-not $SkipBuild) {
  Log "## Build: $EditorTarget Win64 Development"
  $buildLog = Join-Path $ResultsDir ("build-" + $Stamp + ".log")
  $buildArgs = @($EditorTarget, "Win64", "Development", "-Project=$Project", "-WaitMutex")
  & $BuildBat @buildArgs | Tee-Object -FilePath $buildLog | Out-Null
  $buildExit = $LASTEXITCODE
  $errLines = @(Select-String -Path $buildLog -Pattern 'error C\d+|error LNK|: error' -ErrorAction SilentlyContinue | Select-Object -First 8)
  $buildOk = ($buildExit -eq 0 -and $errLines.Count -eq 0)
  if ($buildOk) { Log "- Result: **PASS**" }
  else {
    Log "- Result: **FAIL** (exit $buildExit)"
    foreach ($e in $errLines) { Log ("  - ``" + $e.Line.Trim() + "``") }
  }
  Log "- Build log (local only): ``$buildLog``"
  Log ""
  if (-not $buildOk) {
    Log "## Verdict: BUILD FAILED"
    Log ""
    Log "Tests not reached. See VERIFY_CORTEX_GEMS.md for the version-sensitive anim APIs to check first."
    Save-And-Exit 1
  }
} else {
  Log "## Build: skipped (-SkipBuild)"
  Log ""
}

# Phase 2: ensure a live editor
function Get-CortexPort {
  $pf = Get-ChildItem (Join-Path $SavedDir "CortexPort-*.txt") -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
  if (-not $pf) { return $null }
  $raw = (Get-Content $pf.FullName -Raw).Trim()
  if ($raw -match '"port"\s*:\s*(\d+)') { return [int]$Matches[1] }
  if ($raw -match '^\d+$') { return [int]$raw }
  return $null
}
function Test-CortexPort($p) {
  try { $c = New-Object Net.Sockets.TcpClient; $c.Connect("127.0.0.1", $p); $c.Close(); return $true }
  catch { return $false }
}

Log "## Editor"
if ($LaunchEditor) {
  Get-ChildItem (Join-Path $SavedDir "CortexPort-*.txt") -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  $exe = Join-Path $Engine "Engine\Binaries\Win64\UnrealEditor.exe"
  $eArgs = @($Project, "-nosplash", "-nopause", "-AutoDeclinePackageRecovery")
  if ($SafeDDC) { $eArgs += "-ddc=InstalledNoZenLocalFallback" }
  Log "- Launching editor"
  Start-Process -FilePath $exe -ArgumentList $eArgs | Out-Null
}

$port = $null
$deadline = (Get-Date).AddSeconds(240)
Log "- Waiting for CortexCore (up to 240s)"
while ((Get-Date) -lt $deadline) {
  $port = Get-CortexPort
  if ($port -and (Test-CortexPort $port)) { break }
  Start-Sleep -Seconds 5
}
if (-not ($port -and (Test-CortexPort $port))) {
  Log "- **No reachable editor** in $SavedDir"
  Log ""
  Log "## Verdict: EDITOR NOT REACHABLE"
  Save-And-Exit 1
}
Log "- Editor ready on port $port"
Log ""

# Phase 3: e2e self-test
Log "## E2E self-test"
$testLog = Join-Path $ResultsDir ("pytest-" + $Stamp + ".log")
Push-Location $MCPDir
try {
  & uv sync | Out-Null
  & uv run pytest tests/test_cortex_gems_e2e.py -v | Tee-Object -FilePath $testLog | Out-Null
  $testExit = $LASTEXITCODE
} finally { Pop-Location }

$summary = (Select-String -Path $testLog -Pattern '\d+ (passed|failed|skipped)' -ErrorAction SilentlyContinue | Select-Object -Last 1)
if ($summary) { Log ("- Summary: ``" + $summary.Line.Trim() + "``") }
$fails = @(Select-String -Path $testLog -Pattern 'FAILED ' -ErrorAction SilentlyContinue)
foreach ($f in $fails) { Log ("  - ``" + $f.Line.Trim() + "``") }
Log "- Pytest log (local only): ``$testLog``"
Log ""

if ($testExit -eq 0) {
  Log "## Verdict: PASS - builds on $EngineVersion and all e2e tests green."
  Save-And-Exit 0
} else {
  Log "## Verdict: TESTS FAILED - build OK, e2e reported failures (see above)."
  Save-And-Exit 1
}

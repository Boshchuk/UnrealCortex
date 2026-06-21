<#
  Commit the verification result reports (*.md) and push to the current branch so
  results sync back via git. Called by verify_ue56.ps1. Logs (*.log) are gitignored.
#>
param([string]$Stamp = "")
$ErrorActionPreference = "Continue"

# Repo root = two levels up from this tests/ dir (...\UnrealCortex)
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Push-Location $RepoRoot
try {
  & git add "MCP/tests/verify-results"
  & git commit -m "verify: UE results $Stamp [skip ci]"
  if ($LASTEXITCODE -eq 0) {
    & git push origin HEAD
    if ($LASTEXITCODE -eq 0) { Write-Host "Synced results to origin." }
    else { Write-Host "Committed locally; push failed - run: git push origin HEAD" }
  } else {
    Write-Host "Nothing to commit (no new report)."
  }
} finally { Pop-Location }

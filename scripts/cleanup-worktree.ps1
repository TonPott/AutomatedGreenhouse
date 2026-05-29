$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$RepoFullPath = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)

$LocalDirectories = @(
    ".build",
    ".arduino-cache",
    ".local",
    ".arduino"
)

foreach ($Directory in $LocalDirectories) {
    $Target = Join-Path $RepoRoot $Directory
    $TargetFullPath = [System.IO.Path]::GetFullPath($Target)

    if (-not $TargetFullPath.StartsWith($RepoFullPath + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside repository: $TargetFullPath"
    }

    if (Test-Path -LiteralPath $TargetFullPath) {
        Remove-Item -LiteralPath $TargetFullPath -Recurse -Force
        Write-Host "Removed $Directory"
    } else {
        Write-Host "Skipped $Directory (not present)"
    }
}

Write-Host "Worktree cleanup complete. Shared Arduino CLI toolchain was not touched."

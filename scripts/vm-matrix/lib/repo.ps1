#
# Where the repository is, and how a path relative to it is resolved.
#
# Every script under scripts\vm-matrix\ takes paths from a config file or from
# the command line, and every one of them accepts a repository-relative path -
# `out\post-release`, `vm\win2k.img` - because that is what the committed
# sample config and every runbook are written in. Five of them carried a
# byte-identical private copy of the resolver, along with a private
# `..\..`-relative derivation of the repository root (the 2026-09-07 audit's
# J6). A resolver that five files agree on today is one that four files agree
# on after the next edit.
#
# `Resolve-RepoPath` does not read a `$repo` from its caller's scope, which is
# what the copies did. PowerShell would have let it - a dot-sourced function
# finds the caller's variables at call time - but that makes the function's
# answer depend on a name the caller happens to have set, and a caller that
# renamed it would get a silently different root rather than an error.
#

function Get-VmMatrixRepoRoot {
    # ..\..\.. from lib\: lib -> vm-matrix -> scripts -> the repository.
    return (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
}

# An empty path stays empty rather than becoming the repository root: callers
# use "" to mean "not configured", and turning that into a real directory is
# how an unset OutDir would quietly become the repository itself.
function Resolve-RepoPath {
    param([string]$P)
    if ([string]::IsNullOrWhiteSpace($P)) { return "" }
    if ([IO.Path]::IsPathRooted($P)) { return $P }
    return (Join-Path (Get-VmMatrixRepoRoot) $P)
}

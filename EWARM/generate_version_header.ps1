Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$outFile = Join-Path $repoRoot "APP\\Inc\\Purge_Version_Generated.h"

$gitSha = "gnogit"
$buildTime = Get-Date -Format "yyyy-MM-dd HH:mm:ss"

$gitCmd = Get-Command git -ErrorAction SilentlyContinue
if ($null -ne $gitCmd)
{
    try
    {
        $rev = (& git -C $repoRoot rev-parse --short=8 HEAD).Trim()
        if (-not [string]::IsNullOrWhiteSpace($rev))
        {
            $gitSha = "g$rev"
        }
    }
    catch
    {
        $gitSha = "gnogit"
    }
}

$header = @"
#ifndef __PURGE_VERSION_GENERATED_H
#define __PURGE_VERSION_GENERATED_H

#define PURGE_FW_GIT_SHA "$gitSha"
#define PURGE_FW_BUILD_TIME "$buildTime"

#endif /* __PURGE_VERSION_GENERATED_H */
"@

$existing = ""
if (Test-Path -LiteralPath $outFile)
{
    $existing = Get-Content -LiteralPath $outFile -Raw
}

if ($existing -ne $header)
{
    Set-Content -LiteralPath $outFile -Value $header -Encoding ascii -NoNewline
}

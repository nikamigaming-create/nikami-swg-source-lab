function Get-NikamiClientToolsRoot {
    param([string]$ScriptDir)

    return (Resolve-Path (Join-Path $ScriptDir "..\..")).Path
}

function Get-NikamiWorkspaceRoot {
    param([string]$ClientToolsRoot)

    $override = [Environment]::GetEnvironmentVariable("NIKAMI_SWG_WORKSPACE", "Process")
    if ([string]::IsNullOrWhiteSpace($override)) {
        $override = [Environment]::GetEnvironmentVariable("NIKAMI_SWG_WORKSPACE", "User")
    }
    if (![string]::IsNullOrWhiteSpace($override)) {
        return (Resolve-Path -LiteralPath $override).Path
    }

    $resolvedClientTools = (Resolve-Path -LiteralPath $ClientToolsRoot).Path
    $sourceRoot = Split-Path -Parent $resolvedClientTools
    if ((Split-Path -Leaf $resolvedClientTools) -ieq "client-tools" -and (Split-Path -Leaf $sourceRoot) -ieq "source") {
        return (Split-Path -Parent $sourceRoot)
    }

    return (Split-Path -Parent $resolvedClientTools)
}

function Get-NikamiRuntimeClientRoot {
    param([string]$WorkspaceRoot)

    $candidates = @(
        (Join-Path $WorkspaceRoot "runtime\client\SWGSource Client v3.0"),
        (Join-Path $WorkspaceRoot "runtime\client"),
        (Join-Path $WorkspaceRoot "cleanroom\runtime-official-baseline")
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    return $candidates[0]
}

function Get-NikamiProofRoot {
    param([string]$WorkspaceRoot)
    return (Join-Path $WorkspaceRoot "proofs")
}

function Get-NikamiLogRoot {
    param([string]$WorkspaceRoot)
    return (Join-Path $WorkspaceRoot "logs")
}

function Get-NikamiCleanroomRoot {
    param([string]$WorkspaceRoot)
    return (Join-Path $WorkspaceRoot "cleanroom")
}

function Get-NikamiArtifactsRoot {
    param([string]$WorkspaceRoot)
    return (Join-Path $WorkspaceRoot "artifacts")
}

function Get-NikamiDownloadsRoot {
    param([string]$WorkspaceRoot)
    return (Join-Path $WorkspaceRoot "downloads")
}

function Get-TestPath([string]$Root, [string]$Relative) {
    $rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    if ([IO.Path]::IsPathRooted($Relative)) { throw "Expected a relative path: $Relative" }
    $full = [IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    if (-not $full.StartsWith($rootPath + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path outside test root: $Relative"
    }
    for ($part = $full; $part.Length -ge $rootPath.Length; $part = Split-Path $part -Parent) {
        $item = Get-Item -LiteralPath $part -Force -ErrorAction SilentlyContinue
        if ($item -and ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Reparse point in test path: $part"
        }
    }
    return $full
}

function ConvertTo-FlatOutHashtable($Value) {
    if ($Value -is [pscustomobject]) {
        $converted = @{}
        foreach ($property in $Value.PSObject.Properties) {
            $converted[$property.Name] = ConvertTo-FlatOutHashtable $property.Value
        }
        return $converted
    }
    if ($Value -is [array]) { return ,@($Value | ForEach-Object { ConvertTo-FlatOutHashtable $_ }) }
    return $Value
}

function Read-FlatOutJson([string]$Path) {
    $json = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
    if ($PSVersionTable.PSVersion.Major -ge 7) { return ($json | ConvertFrom-Json -AsHashtable) }
    return (ConvertTo-FlatOutHashtable ($json | ConvertFrom-Json))
}

function Get-FlatOutFileHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
    finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}

function Get-TestInventory([string]$Root, [string[]]$Files, [string[]]$Directories) {
    $inventory = @{}
    foreach ($relative in $Files) {
        $path = Get-TestPath $Root $relative
        if (Test-Path -LiteralPath $path) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Expected file: $path" }
            $inventory[$relative] = Get-FlatOutFileHash $path
        }
    }
    $queue = [Collections.Generic.Queue[string]]::new()
    foreach ($relative in $Directories) { $queue.Enqueue($relative) }
    while ($queue.Count) {
        $path = Get-TestPath $Root $queue.Dequeue()
        if (-not (Test-Path -LiteralPath $path)) { continue }
        foreach ($item in Get-ChildItem -LiteralPath $path -Force) {
            $relative = $item.FullName.Substring([IO.Path]::GetFullPath($Root).TrimEnd('\').Length + 1)
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point: $relative" }
            if ($item.PSIsContainer) { $queue.Enqueue($relative) }
            else { $inventory[$relative] = Get-FlatOutFileHash $item.FullName }
        }
    }
    return $inventory
}

function Test-SameInventory($Expected, $Actual) {
    if ($Expected.Count -ne $Actual.Count) { return $false }
    foreach ($key in $Expected.Keys) {
        if ($Actual[$key] -ne $Expected[$key]) { return $false }
    }
    return $true
}

function Save-FlatOutTestState([string]$GameDir, [string]$RunDir, [string[]]$Files, [string[]]$Directories) {
    $before = Get-TestInventory $GameDir $Files $Directories
    $backup = Join-Path $RunDir 'backup'
    [void][IO.Directory]::CreateDirectory($backup)
    foreach ($relative in $before.Keys) {
        $destination = Get-TestPath $backup $relative
        [void][IO.Directory]::CreateDirectory((Split-Path $destination -Parent))
        Copy-Item -LiteralPath (Get-TestPath $GameDir $relative) -Destination $destination
    }
    if (-not (Test-SameInventory $before (Get-TestInventory $backup $Files $Directories))) {
        throw 'Backup verification failed.'
    }
    $state = @{ GameDir = $GameDir; Files = $Files; Directories = $Directories; Before = $before }
    $pending = Join-Path $RunDir 'rollback.tmp'
    $state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $pending -Encoding utf8
    Move-Item -LiteralPath $pending -Destination (Join-Path $RunDir 'rollback.json')
}

function Restore-FlatOutTestState([string]$GameDir, [string]$RunDir, [string[]]$Files, [string[]]$Directories) {
    $state = Read-FlatOutJson (Join-Path $RunDir 'rollback.json')
    if ($state.GameDir -ne $GameDir -or ($state.Files -join '|') -ne ($Files -join '|') -or
        ($state.Directories -join '|') -ne ($Directories -join '|')) { throw 'Rollback scope does not match the runner.' }
    $prior = $state.Before
    $backup = Join-Path $RunDir 'backup'
    if (-not (Test-SameInventory $prior (Get-TestInventory $backup $Files $Directories))) {
        throw 'Rollback backup is incomplete or has changed.'
    }
    $current = Get-TestInventory $GameDir $Files $Directories
    foreach ($relative in $prior.Keys) {
        if ($current[$relative] -eq $prior[$relative]) { continue }
        $target = Get-TestPath $GameDir $relative
        [void][IO.Directory]::CreateDirectory((Split-Path $target -Parent))
        Copy-Item -LiteralPath (Get-TestPath $backup $relative) -Destination $target -Force
    }
    foreach ($relative in $current.Keys) {
        if (-not $prior.Contains($relative)) { Remove-Item -LiteralPath (Get-TestPath $GameDir $relative) }
    }
    if (-not (Test-SameInventory $prior (Get-TestInventory $GameDir $Files $Directories))) {
        throw 'Restored installation did not match its backup.'
    }
    Move-Item -LiteralPath (Join-Path $RunDir 'rollback.json') -Destination (Join-Path $RunDir 'restored.json')
    # All filenames were validated through the backup inventory above.
    foreach ($relative in $prior.Keys) { Remove-Item -LiteralPath (Get-TestPath $backup $relative) }
}

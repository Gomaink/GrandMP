$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\CdbHelpParser.ps1')

$cases = @(
    @{
        Name = 'Windows SDK usage with angle-bracket PID'
        Text = "Options:`n-c `"<command>`"`n-cf <file>`n-p <pid>`n-pd"
        Expected = $true
    },
    @{
        Name = 'uppercase options and variable spacing'
        Text = "  -C     `"commands`"`r`n -CF   <FILE>`r`n    -P   < PID >`r`n -PD `t detach"
        Expected = $true
    },
    @{
        Name = 'legacy PID spelling and inline usage'
        Text = 'usage: cdb -c "command" -cf file [ -p PID ] -pd'
        Expected = $true
    },
    @{
        Name = 'reject missing process ID option'
        Text = "-c command`n-pn name`n-pd"
        Expected = $false
    },
    @{
        Name = 'reject unrelated process placeholder'
        Text = "-c command`n-p <name>`n-pd"
        Expected = $false
    },
    @{
        Name = 'reject missing command option'
        Text = "-p <pid>`n-pd"
        Expected = $false
    },
    @{
        Name = 'detach is not required when explicit .detach is used'
        Text = "-c command`n-cf commands.wds`n-p <pid>`n-pdx"
        Expected = $true
    },
    @{
        Name = 'reject missing script option'
        Text = "-c command`n-p <pid>`n-pd"
        Expected = $false
    }
)

$failures = [System.Collections.Generic.List[string]]::new()
foreach ($case in $cases) {
    $actual = (Test-CdbHelpParameters -HelpText $case.Text).Valid
    if ($actual -ne $case.Expected) {
        $failures.Add("$($case.Name): esperado $($case.Expected), recebido $actual")
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output ("CDB help parser: {0}/{0} testes aprovados." -f $cases.Count)

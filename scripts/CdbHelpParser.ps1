function Test-CdbHelpParameters {
    [CmdletBinding()]
    param([Parameter(Mandatory)][AllowEmptyString()][string]$HelpText)

    # Match option tokens rather than exact help prose: SDK releases differ in
    # capitalization, spacing, and whether placeholders are written as PID or
    # <pid>. Requiring whitespace after -p also prevents -pd/-pn being accepted
    # as the attach-by-PID option.
    $commandOption = [regex]::IsMatch(
        $HelpText,
        '(?i)(?<![\w-])-c(?![\w-])\s+(?:"[^"]*"|<[^>]+>|\S+)'
    )
    $scriptFileOption = [regex]::IsMatch(
        $HelpText,
        '(?i)(?<![\w-])-cf(?![\w-])\s+(?:<[^>]+>|\S+)'
    )
    $processOption = [regex]::IsMatch(
        $HelpText,
        '(?i)(?<![\w-])-p(?![\w-])\s+(?:<\s*(?:pid|process\s*id)\s*>|pid\b|process\s+id\b)'
    )
    return [pscustomobject]@{
        Command = $commandOption
        ScriptFile = $scriptFileOption
        ProcessId = $processOption
        Valid = ($commandOption -and $scriptFileOption -and $processOption)
    }
}

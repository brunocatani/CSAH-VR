param(
    [Parameter(Mandatory = $true)]
    [string] $RuntimeDirectory
)

$ErrorActionPreference = 'Stop'

$expectedNames = @(
    'sl.interposer.dll',
    'sl.common.dll',
    'sl.dlss.dll',
    'nvngx_dlss.dll'
)
$RuntimeFiles = @($expectedNames | ForEach-Object {
    Join-Path -Path $RuntimeDirectory -ChildPath $_
})

foreach ($path in $RuntimeFiles) {
    $item = Get-Item -LiteralPath $path
    if ($item.Length -le 0) {
        throw "Streamline payload is empty: '$path'."
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $item.FullName
    if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
        throw "Streamline payload signature is not valid: '$path' ($($signature.Status))."
    }
    if (-not $signature.SignerCertificate -or
        $signature.SignerCertificate.Subject -notmatch '(^|, )O=NVIDIA Corporation(,|$)') {
        throw "Streamline payload signer is not NVIDIA Corporation: '$path'."
    }
    Write-Host "Verified NVIDIA-signed Streamline payload $($item.Name)"
}

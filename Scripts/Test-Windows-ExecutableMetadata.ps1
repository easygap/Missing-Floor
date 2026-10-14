[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$Executable,
	[string]$ExpectedProductName = 'Missing Floor',
	[string]$ExpectedVersion = '0.10.0',
	[string]$ExpectedCompanyName = 'easygap',
	[string]$ExpectedCopyright =
		'Copyright 2026 easygap. All rights reserved.',
	[string]$ExpectedInternalName = 'IndieGame'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
	throw "Executable does not exist: $Executable"
}

$resolvedExecutable = (Resolve-Path -LiteralPath $Executable).Path
$executableItem = Get-Item -LiteralPath $resolvedExecutable
if ($executableItem.Length -le 0) {
	throw "Executable is empty: $resolvedExecutable"
}

$versionInfo = $executableItem.VersionInfo
$actual = [ordered]@{
	FileDescription = [string]$versionInfo.FileDescription
	FileVersion = [string]$versionInfo.FileVersion
	ProductName = [string]$versionInfo.ProductName
	ProductVersion = [string]$versionInfo.ProductVersion
	CompanyName = [string]$versionInfo.CompanyName
	LegalCopyright = [string]$versionInfo.LegalCopyright
	InternalName = [string]$versionInfo.InternalName
	OriginalFilename = [string]$versionInfo.OriginalFilename
}
$expected = [ordered]@{
	FileDescription = $ExpectedProductName
	FileVersion = $ExpectedVersion
	ProductName = $ExpectedProductName
	ProductVersion = $ExpectedVersion
	CompanyName = $ExpectedCompanyName
	LegalCopyright = $ExpectedCopyright
	InternalName = $ExpectedInternalName
	OriginalFilename = [IO.Path]::GetFileName($resolvedExecutable)
}

foreach ($field in $expected.Keys) {
	$actualValue = $actual[$field].Trim()
	$expectedValue = $expected[$field].Trim()
	if ($actualValue -cne $expectedValue) {
		throw (
			"Windows executable metadata mismatch for ${field}: " +
			"expected='$expectedValue' actual='$actualValue' " +
			"executable='$resolvedExecutable'")
	}
}
if ($actual.FileVersion -match '(?i)(\+\+UE|Release-|CL-)' -or
	$actual.ProductVersion -match '(?i)(\+\+UE|Release-|CL-)') {
	throw "Engine build metadata leaked into the product version: $resolvedExecutable"
}

$sha256 = (
	Get-FileHash -Algorithm SHA256 -LiteralPath $resolvedExecutable
).Hash
Write-Host (
	'WINDOWS_EXECUTABLE_METADATA PASS ' +
	"product='$($actual.ProductName)' " +
	"version='$($actual.ProductVersion)' " +
	"company='$($actual.CompanyName)' " +
	"original='$($actual.OriginalFilename)' " +
	"sha256=$sha256"
) -ForegroundColor Green

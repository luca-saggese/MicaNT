# IPHLPAPI

The standalone LuceOS `iphlpapi.dll` exports the six IP Helper entry points
currently declared in its system DLL profile: `GetAdaptersAddresses`,
`GetAdaptersInfo`, `GetBestInterfaceEx`, `GetIfTable2`, `FreeMibTable`, and
`GetNetworkParams`.

No network interface provider is connected to this DLL yet. Enumeration and
route queries return `ERROR_NOT_SUPPORTED` (50); APIs with output pointers
clear those outputs before returning. `FreeMibTable` is a no-op because the
current `GetIfTable2` implementation never allocates a table. These exports do
not report fabricated adapters, interfaces, indices, or successful network
results.

This is an explicit unsupported profile, not a complete Windows 11
`iphlpapi.dll` export inventory. The remaining public exports and documented
parameter behavior still need to be added as the network subsystem grows.

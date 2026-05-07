# SCC functional-test map

This directory is reserved for `SCCRoundTripMap.umap`, an Unreal
content asset (binary) that hosts an instance of
`ASCCRoundTripFunctionalTest`. The asset is created in-editor; binary
maps are not committed to source control to avoid merge conflicts.

## One-time setup

In the Unreal Editor with the SCC plugin enabled:

1. **Create a new map** at `Content/SCC/Tests/SCCRoundTripMap.umap`
   using `File → New Level → Empty Level`.
2. **Place an `ASCCRoundTripFunctionalTest` actor** in the map
   (`Place Actors → All Classes → SCCRoundTripFunctionalTest` or
   drag from the C++ Class browser under
   `SCC → ASCCRoundTripFunctionalTest`).
3. **Configure the test parameters** on the actor's Details panel:
   - `Test Width`: 16
   - `Test Height`: 16
   - `Test Profile`: `Lossless`
4. **Save** the map.

## Running

- **In-editor**: open `Window → Test Automation → Functional Tests`,
  load the map, and click "Start Tests".
- **Headless CI** (preferred): use the automation tests defined in
  `Source/SCC/Private/SCCAutomationTests.cpp`. They run without a
  map via:

  ```
  UnrealEditor-Cmd <Project>.uproject \
      -ExecCmds="Automation RunTests SCC." \
      -unattended -nopause -nullrhi -log
  ```

  Test names: `SCC.RoundTrip.SyntheticFrame`,
  `SCC.RoundTrip.EncodeWithoutBegin`,
  `SCC.RoundTrip.DecodeOnGarbage`,
  `SCC.RoundTrip.AllProfiles`.

The headless tests cover the same logic as the FunctionalTest map and
do not require any binary content to be checked in. [Ultrathink #2]

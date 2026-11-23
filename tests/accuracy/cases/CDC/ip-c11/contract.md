# C11

Reset interaction / raw asynchronous release

## Operating contract

`arst_n` asynchronously asserts and may deassert at any time, including arbitrarily close to a `clk` edge. `q` is used immediately after deassertion. No reset-release synchronizer or external recovery/removal guarantee is present.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

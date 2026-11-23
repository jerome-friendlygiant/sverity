# C02

Synchronizer depth / direct async sample

## Operating contract

`async_req` may change at any time relative to `clk_d`. It is a level that remains asserted until downstream logic observes it. No reset is used.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

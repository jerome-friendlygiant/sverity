# C03

Synchronizer chain / fanout

## Operating contract

`async_req` may change at any time relative to `clk_d` and remains asserted until acknowledged externally. `sync1` is not consumed elsewhere. No reset is used; the output is ignored until two destination edges have occurred after power-up.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

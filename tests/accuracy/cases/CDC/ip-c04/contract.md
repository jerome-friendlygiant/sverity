# C04

Multi-bit transfer / independent capture

## Operating contract

`data_s` is registered in the `clk_s` domain and may change on every source edge. The destination consumes all eight bits together as a binary command; there is no handshake, encoded redundancy, or stability interval.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

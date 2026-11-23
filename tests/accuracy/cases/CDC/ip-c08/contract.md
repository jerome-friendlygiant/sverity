# C08

FIFO pointer transfer / binary pointer

## Operating contract

`wr_bin` is a 4-bit binary write pointer updated in `wr_clk` and may increment by one each write. `wr_seen` is used by read-side full/empty logic. No memory model or reset behavior is specified, so FIFO startup and collision behavior remain unmodeled.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

# C09

FIFO pointer transfer / Gray pointer stages

## Operating contract

`wr_gray` is a registered Gray-coded pointer from `wr_clk`, changing by at most one Gray bit per source edge. `wr_seen` feeds read-domain pointer comparison. Reset, memory collision semantics, full/empty correctness, physical inter-bit skew, and implementation metastability behavior are outside this module and are not asserted by this input.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

# C10

Clock control / combinational gate

## Operating contract

`en` is an ordinary data-domain signal and can change at any phase of `clk`. It is not driven by an integrated clock-gating cell and is not constrained to change while `clk` is low.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

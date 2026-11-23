# C06

Event protocol / narrow pulse

## Operating contract

`pulse_s` is a one-`clk_s`-cycle event and can occur on consecutive source cycles. No event-rate bound, pulse stretcher, or acknowledgment is present.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

# rfc1-h32 contract

## Summary

An external asynchronous reset is conditioned by a local reset bridge before releasing the functional register. Everything runs from clk_a.

## Clocks

- `clk_a`: period 10.0 ns, top-level port `clk_a`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

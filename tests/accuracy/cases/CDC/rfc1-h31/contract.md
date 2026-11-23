# rfc1-h31 contract

## Summary

An input byte is registered and conditionally loaded into an output register under a registered enable. All registers use clk_a and reset is synchronous.

## Clocks

- `clk_a`: period 10.0 ns, top-level port `clk_a`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

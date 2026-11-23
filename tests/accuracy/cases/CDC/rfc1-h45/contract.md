# rfc1-h45 contract

## Summary

A command byte is registered and sent to an externally supplied macro. The macro response is registered at the wrapper output. The macro model is provided by the vendor at integration.

## Clocks

- `clk_io`: period 10.0 ns, top-level port `clk_io`

## Files

- `design.sv`: RTL, top module `top`.
- `intent.sdc`: timing intent (`set_units -time ns`).

## Review request

Review `design.sv` against `intent.sdc` for clock-domain and reset hazards. Report your findings and any assumption you could not discharge.

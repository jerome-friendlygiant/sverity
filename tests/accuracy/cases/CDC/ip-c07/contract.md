# C07

Qualified payload / stability unknown

## Operating contract

`flag_s` is an externally maintained source-domain level. The source interface specification is intentionally unavailable: it does not say whether `payload_s` is stable while `flag_s` is high, or whether the destination uses payload only when `flag_d` is high. Preserve this missing contract as unknown.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

# C12

Reset interaction / synchronized release

## Operating contract

`arst_n` may assert asynchronously. Deassertion can occur at any phase. All local state is held reset until `rst_n_local` rises after two `clk` edges; downstream users must use `rst_n_local`, not raw `arst_n`, for reset release. The implementation still requires physical recovery/removal and metastability review.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

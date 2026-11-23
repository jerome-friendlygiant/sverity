# C05

Multi-bit handshake / held payload

## Operating contract

`rst_n` asserts asynchronously and is deasserted with recovery/removal margin to both clocks; both clocks then run continuously for at least two edges before `send` is permitted. `send` is a one-cycle source-domain pulse only when the source has no outstanding item; it is held low until that item is accepted. `payload_s` is stable before the accepting source edge and may change only on that edge or when no request is outstanding. `accept_d` is synchronous to `clk_d`; the receiver accepts each `valid_d` item in one cycle. The receiver samples the bundled payload after the synchronized request has crossed. Contract review must verify request/ack ordering and that the payload remains stable from launch through destination capture.

## Input boundary

This directory is the complete input for the case. `case.sv` is the elaboration top; `constraints.sdc` records only the clock facts stated above. No library cells or external RTL are required.

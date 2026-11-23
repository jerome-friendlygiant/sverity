# CDC-RFC1-T29 interface contract

An upstream wrapper is expected to hold src_data, but no duration/ack contract is supplied. Disclosure required; safety cannot be decided from this RTL.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

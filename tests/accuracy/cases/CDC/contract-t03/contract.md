# CDC-RFC1-T03 interface contract

The two command bits encode one atomic opcode and source may change both on one src_clk edge. No Gray or independently meaningful-bit contract.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

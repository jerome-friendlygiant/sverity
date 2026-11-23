# CDC-RFC1-T11 interface contract

This is a two-domain FIFO pointer observation: rd_bin is the read pointer, dst_clk is write domain, depth is eight. Pointer must be Gray-coded and synchronized; this intentionally crosses binary pointer directly.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

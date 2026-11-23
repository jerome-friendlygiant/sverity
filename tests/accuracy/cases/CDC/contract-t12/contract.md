# CDC-RFC1-T12 interface contract

`rd_gray` is a registered Gray encoding of a binary read pointer. The write-domain consumer uses the upper bits after two destination stages, but consumes bit 0 directly from the asynchronous pointer.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

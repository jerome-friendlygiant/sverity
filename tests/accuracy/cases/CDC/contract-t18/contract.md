# CDC-RFC1-T18 interface contract

Source asserts req with payload stable, holds both until synchronized src_ack, lowers req, and waits for src_ack low before the next transaction. One transaction at a time; independent bench checks one delivery and coherent payload.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

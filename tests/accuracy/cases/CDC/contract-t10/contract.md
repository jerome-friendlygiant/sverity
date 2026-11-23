# CDC-RFC1-T10 interface contract

Intended four-phase request/ack handshake: source must hold request and payload until synchronized acknowledgment, then wait for request low. Acknowledgment is not returned through a synchronizer and source has no progress state.

Clock periods in this case are `src_clk=10 ns`, `dst_clk=14 ns`; clocks are asynchronous by SDC. `rst_n` is asynchronous active-low assertion. No physical metastability, skew, recovery/removal, or clock-tree claim is established by this ideal RTL/SDC model.

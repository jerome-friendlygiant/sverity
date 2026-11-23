// sverity-args: --top top --sdc @DIR@/registered_bit_modes.sdc
// Bit 0 retains an asynchronous min contract beside a same-clock max contract; bit 1 is the safe same-clock lookalike.
// expect-count: CDC-029 ERROR 1
// expect-count: CDC-029 INFO 1
module top(input logic src_clk, dst_clk, input logic [1:0] data, output logic async_q, sync_q);
  always_ff @(posedge dst_clk) async_q <= data[0];
  always_ff @(posedge dst_clk) sync_q <= data[1];
endmodule

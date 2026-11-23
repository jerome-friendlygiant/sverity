// sverity-args: --top top --sdc @DIR@/explicit_async_mixed.sdc
// The same explicit asynchronous launch is contained by a complete two-stage chain.
// expect: CDC-029 INFO
// expect-none: CDC-029 ERROR
module top(input logic src_clk, dst_clk, req, output logic first, second);
  always_ff @(posedge dst_clk) begin first <= req; second <= first; end
endmodule

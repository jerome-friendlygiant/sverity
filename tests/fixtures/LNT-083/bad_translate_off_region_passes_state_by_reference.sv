// sverity-args: --top top
// The task writes its `ref` formal, so the register passed to it inside the region is written there and read outside it.
// expect: LNT-083 WARNING 9
// expect-none: LNT-083 INFO
module top(input logic clk, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] r;
  task automatic poke(ref logic [3:0] x); x = 4'h5; endtask
  always_ff @(posedge clk) q <= d + r;
  // synopsys translate_off
  logic seen;
  always @(posedge clk) begin seen <= d[0]; poke(r); end
  // synopsys translate_on
endmodule

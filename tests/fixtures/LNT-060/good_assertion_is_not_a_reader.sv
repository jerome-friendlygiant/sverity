// sverity-args: --top top
// A concurrent assertion samples its operands before the edge: it does not race with the blocking write.
// expect-none: LNT-060
module top(input logic clk, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] t;
  always_ff @(posedge clk) begin t = d + 1; q <= t; end
  assert property (@(posedge clk) t != 4'hF);
endmodule

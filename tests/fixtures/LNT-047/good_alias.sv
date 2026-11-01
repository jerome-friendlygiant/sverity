// sverity-args: --top top
// expect-none: LNT-047
module top(input logic clk, a, output logic q, output logic g_out);
  logic g;
  assign g = clk;
  always @(posedge g) q <= a;
  always_ff @(posedge clk) g_out <= a;
endmodule

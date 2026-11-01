// sverity-args: --top top
// expect-none: LNT-047
module top(input logic clk, a, output logic q);
  logic s;
  always_ff @(posedge clk) s <= a;
  always_ff @(posedge s) q <= a;
endmodule

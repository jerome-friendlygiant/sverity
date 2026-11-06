// sverity-args: --top top
// Disjoint bits of one vector from two procedures, and an initial value, are not two drivers.
// expect-none: LNT-009
module top(input logic clk, d, output logic [3:0] v, output logic q);
  always @(posedge clk) v[1:0] <= 2'b01;
  always @(posedge clk) v[3:2] <= {d, d};
  initial q = 1'b0;
  always @(posedge clk) q <= d;
endmodule

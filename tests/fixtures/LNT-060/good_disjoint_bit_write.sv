// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk, output logic y);
  logic [1:0] q;
  always @(posedge clk) q[0] = 1;
  always_comb y = q[1];
endmodule

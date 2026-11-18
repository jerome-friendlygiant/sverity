// sverity-args: --top top
// expect-unresolved: LNT-060
module top(input logic clk, sel, output logic y);
  logic [1:0] q;
  always @(posedge clk) q[0] = 1;
  assign y = q[sel];
endmodule

// sverity-args: --top top
// expect-confidence: LNT-060 established
module top(input logic clk, output logic y);
  logic [1:0] q;
  always @(posedge clk) q[0]++;
  assign y = q[0];
endmodule

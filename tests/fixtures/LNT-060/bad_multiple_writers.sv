// sverity-args: --top top
// expect: LNT-060 WARNING
module top(input logic clk, output logic y);
  logic q;
  always @(posedge clk) begin q = 1; y <= q; end
  always @(posedge clk) q = 0;
endmodule

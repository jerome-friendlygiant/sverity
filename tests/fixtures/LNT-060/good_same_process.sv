// sverity-args: --top top
// expect-none: LNT-060
module top(input logic clk, output logic y);
  logic q;
  always @(posedge clk) begin q = 1; y <= q; end
endmodule

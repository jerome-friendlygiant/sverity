// sverity-args: --top top
// expect-unresolved: LNT-060
module top(input logic clk, output logic y);
  logic q;
  always @(posedge clk) begin
    fork q = 1; join_none
    y <= q;
  end
endmodule

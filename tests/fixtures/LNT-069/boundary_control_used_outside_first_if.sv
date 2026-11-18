// sverity-args: --top top
// The reset is used in the body but not by the first if: undecided, so Heuristic, not the ERROR of a control the block never uses.
// expect: LNT-069 WARNING 5
module top(input logic clk, rst_n, a, output logic q);
  always_ff @(posedge clk or negedge rst_n) begin
    q <= a;
    if (!rst_n) q <= 1'b0;
  end
endmodule

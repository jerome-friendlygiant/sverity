// sverity-args: --top top
// `rst_done` is a status flag, not a reset: `seen` is set by data logic, not by a reset branch that has no other branch.
// expect-none: LNT-020
module top(input logic clk, en, rst_done, d, output logic y, output logic seen);
  logic q;
  always_ff @(posedge clk) begin
    if (en) q <= d;
    if (rst_done) seen <= 1'b1;
  end
  assign y = q;
endmodule

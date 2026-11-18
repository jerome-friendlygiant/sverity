// sverity-args: --top top
// `rst_core_n` is a reset by its name with no doubt (a domain word follows the reset word): `seen` is set only in its reset branch.
// expect: LNT-020 WARNING 7
module top(input logic clk, rst_core_n, d, output logic q, output logic seen);
  always_ff @(posedge clk) begin
    q <= d;
    if (!rst_core_n) seen <= 1'b1;
  end
endmodule

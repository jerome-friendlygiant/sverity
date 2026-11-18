// sverity-args: --top top
// `rst_q` may be a reset or a flag: whether `seen` is reset-only cannot be decided.
// expect: LNT-020 WARNING 7
module top(input logic clk, rst_q, d, output logic q, output logic seen);
  always_ff @(posedge clk) begin
    q <= d;
    if (rst_q) seen <= 1'b1;
  end
endmodule

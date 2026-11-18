// sverity-args: --top top
// `s` is written by the reset and the set branch and never loaded: reset-only, as in a flop with one control.
// expect: LNT-020 WARNING 6
module top(input logic clk, rst_n, set_n, d, output logic q, s);
  always_ff @(posedge clk or negedge rst_n or negedge set_n)
    if (!rst_n) begin q <= 1'b0; s <= 1'b0; end
    else if (!set_n) begin q <= 1'b1; s <= 1'b1; end
    else q <= d;
endmodule

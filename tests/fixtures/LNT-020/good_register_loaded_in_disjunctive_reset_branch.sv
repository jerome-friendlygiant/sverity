// sverity-args: --top top
// `load` makes the branch of `rst || load` a load too. `q` takes `din` there, so it is not reset-only; `flag` stores only the constant `1'b1` in
// that branch and has no other write, so it never takes anything else.
// expect-count: LNT-020 WARNING 1
// expect: LNT-020 WARNING 10
module top(input logic clk, rst, load, set, d, din, output logic q, flag, y);
  always_ff @(posedge clk) begin
    y <= d;
    if (rst || load) q <= rst ? 1'b0 : din;
    if (rst || set) flag <= 1'b1;
  end
endmodule

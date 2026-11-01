// sverity-args: --top top
// the child body is shared by two instances: one finding with count 2
// expect: LNT-043 WARNING 8
// expect-count: LNT-043 WARNING 2
module child(input logic clk, en, rst, d, output logic q);
  always_ff @(posedge clk) begin
    if (en) q <= d;
    if (rst) q <= 1'b0;
  end
endmodule
module top(input logic clk, en, rst, d, output logic q0, q1);
  child u0(clk, en, rst, d, q0);
  child u1(clk, rst, en, d, q1);
endmodule

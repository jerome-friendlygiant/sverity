// sverity-args: --top top
// Only the second instance of `sub` ties both ports to one net: there `posedge a` and `negedge b` are one signal on both edges. The
// first instance has two nets (clock and reset), so the finding names the second.
// expect: LNT-022 WARNING 6
module sub(input logic a, b, d, output logic q);
  always_ff @(posedge a or negedge b) if (!b) q <= 1'b0; else q <= d;
endmodule
module top(input logic clk, rst_n, d, output logic q1, q2);
  sub u(.a(clk), .b(rst_n), .d(d), .q(q1));
  sub v(.a(clk), .b(clk), .d(q1), .q(q2));
endmodule

// sverity-args: --top top
// always_latch commonly uses nonblocking and is not flagged; the child's blocking-in-ff is one finding with 2 occurrences
// expect: LNT-015 WARNING
// expect-count: LNT-015 WARNING 2
module child(input logic clk, d, en, output logic q, output logic l);
  always_ff @(posedge clk) q = d;
  always_latch if (en) l <= d;
endmodule
module top(input logic clk, d, en, output logic [1:0] q, l);
  child u0(clk, d, en, q[0], l[0]);
  child u1(clk, d, en, q[1], l[1]);
endmodule

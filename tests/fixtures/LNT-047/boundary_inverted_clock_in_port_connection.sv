// sverity-args: --top top
// One inversion of a clock in a port connection (`.clk(~clk)`) is glitch-free and built as written: INFO, like `posedge ~clk`. A gate
// there is the hazard (`clk & en`, WARNING), whatever the order of the instances.
// expect: LNT-047 INFO 9
// expect: LNT-047 WARNING 14
// expect-count: LNT-047 INFO 1
// expect-count: LNT-047 WARNING 1
module sub(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule
module top(input logic clk, en, d, output logic q1, q2, q3);
  sub u(.clk(clk), .d(d), .q(q1));
  sub v(.clk(~clk), .d(d), .q(q2));
  sub w(.clk(clk & en), .d(d), .q(q3));
endmodule

// sverity-args: --top top
// The second instance of `sub` is clocked through logic; the first is clocked directly. The rule judges every instance, so the finding
// does not depend on the order of the instances, and it says which instance has it.
// expect: LNT-047 WARNING 10
module sub(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule
module top(input logic clk, en, d, output logic q1, q2);
  sub u(.clk(clk), .d(d), .q(q1));
  sub v(.clk(clk & en), .d(q1), .q(q2));
endmodule

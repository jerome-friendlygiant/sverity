// sverity-args: --top top --disable-rule NO-SUCH-RULE
// Bad + mutation: disabling a nonexistent rule ID triggers CST-019 WARNING Unresolved.
// expect: CST-019 WARNING
// expect-unresolved: CST-019
// revert: --disable-rule NO-SUCH-RULE => --disable-rule LNT-075
// revert-expect-none: CST-019 WARNING
module top(input logic clk, d, output logic q);
  always_ff @(posedge clk) q <= d;
endmodule

// sverity-args: --top top
// Edges of the width predicate: an explicit cast is exempt, and a difference in signedness alone is LNT-005's.
// expect-none: LNT-004
module top(input logic [7:0] a, input logic signed [7:0] s, input logic [3:0] n, output logic [7:0] y, z);
  assign y = 8'(n) + a;             // explicit widening cast: no finding
  assign z = a + s;                 // same width, other signedness: not a width mismatch
endmodule

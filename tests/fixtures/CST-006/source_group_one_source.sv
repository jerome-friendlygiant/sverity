// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A waiver for one of the two grouped sources says nothing about the other: it does not apply.
// expect: RDC-004 WARNING
// expect: CST-022 WARNING
module top(input logic clk, r1, r2, d, output logic q);
  logic x, y;
  always_ff @(posedge clk or negedge r1) if (!r1) x <= 0; else x <= d;
  always_ff @(posedge clk or negedge r1) if (!r1) y <= 0; else y <= ~d;
  // sverity-waive RDC-004 owner="team" reason="x is sequenced externally" signal="top.x"
  always_ff @(posedge clk or negedge r2) if (!r2) q <= 0; else q <= x ^ y;
endmodule

// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A waiver naming the destination register accepts the whole grouped finding.
// expect: RDC-004 WARNING
// expect-none: CST-022
module top(input logic clk, r1, r2, d, output logic q);
  logic x, y;
  always_ff @(posedge clk or negedge r1) if (!r1) x <= 0; else x <= d;
  always_ff @(posedge clk or negedge r1) if (!r1) y <= 0; else y <= ~d;
  // sverity-waive RDC-004 owner="team" reason="q tolerates this reset" signal="top.q"
  always_ff @(posedge clk or negedge r2) if (!r2) q <= 0; else q <= x ^ y;
endmodule

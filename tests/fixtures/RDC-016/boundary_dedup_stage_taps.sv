// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A 3-stage reset synchroniser with reset users at both r2 and r3 and a logic tap at r1
// must emit at most one RDC-016 warning per offending stage (2 total: r1 and r2, never 3).
// expect-count: RDC-016 WARNING 2
module top(input logic clka, rst_n, d1, d2, output logic q1, q2, tap);
  logic r1, r2, r3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {r3, r2, r1} <= 3'b000; else {r3, r2, r1} <= {r2, r1, 1'b1};
  always_ff @(posedge clka or negedge r2) if (!r2) q1 <= 1'b0; else q1 <= d1;
  always_ff @(posedge clka or negedge r3) if (!r3) q2 <= 1'b0; else q2 <= d2;
  assign tap = r1;
endmodule

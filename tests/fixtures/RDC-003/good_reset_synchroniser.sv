// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A 2-stage reset synchroniser: asynchronous assertion, synchronous release in the receiving domain.
// expect: RDC-003 INFO
// expect-none: RDC-003 WARNING
// expect-none: RDC-006
module top(input logic clka, arst_n, d, output logic q);
  logic r1, r2;
  always_ff @(posedge clka or negedge arst_n) if (!arst_n) {r2, r1} <= 2'b00; else {r2, r1} <= {r1, 1'b1};
  always_ff @(posedge clka or negedge r2) if (!r2) q <= 1'b0; else q <= d;
endmodule

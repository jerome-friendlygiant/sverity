// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option RDC-006.min_stages=3
// Opposite-edge receiver must still emit RDC-003 WARNING Unresolved even when RDC-006 also flags shallow depth.
// expect: RDC-006 WARNING
// expect: RDC-003 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  logic r1, r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {r2, r1} <= 2'b00; else {r2, r1} <= {r1, 1'b1};
  always_ff @(negedge clka or negedge r2) if (!r2) q <= 1'b0; else q <= d;
endmodule

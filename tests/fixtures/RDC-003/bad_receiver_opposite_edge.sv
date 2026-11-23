// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Synchroniser runs on posedge clka, but reset user runs on negedge clka: half-cycle release timing is unresolved.
// expect: RDC-003 WARNING
// expect-none: RDC-003 INFO
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  logic r1, r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {r2, r1} <= 2'b00; else {r2, r1} <= {r1, 1'b1};
  always_ff @(negedge clka or negedge r2) if (!r2) q <= 1'b0; else q <= d;
endmodule

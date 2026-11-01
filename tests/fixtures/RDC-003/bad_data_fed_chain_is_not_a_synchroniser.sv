// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: else {r2, r1} <= {r1, en}; => else {r2, r1} <= {r1, 1'b1};
// revert-expect-none: RDC-003 WARNING
// A register chain whose first stage is fed by DATA (not a constant) is not a reset synchroniser.
// expect: RDC-003 WARNING
// expect-none: RDC-003 INFO
module top(input logic clka, arst_n, en, d, output logic q);
  logic r1, r2;
  always_ff @(posedge clka or negedge arst_n) if (!arst_n) {r2, r1} <= 2'b00; else {r2, r1} <= {r1, en};
  always_ff @(posedge clka or negedge r2) if (!r2) q <= 1'b0; else q <= d;
endmodule

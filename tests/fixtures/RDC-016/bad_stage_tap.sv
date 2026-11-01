// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign tap = r1; => assign tap = 1'b0;
// revert-expect-none: RDC-016 WARNING
// MUTATION of RDC-003/good reset synchroniser: the first stage is also used as a functional signal.
// expect: RDC-016 WARNING
module top(input logic clka, rst_n, d, output logic q, tap);
  logic r1, r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {r2, r1} <= 2'b0; else {r2, r1} <= {r1, 1'b1};
  always_ff @(posedge clka or negedge r2) if (!r2) q <= 1'b0; else q <= d;
  assign tap = r1;
endmodule

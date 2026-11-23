// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Unknown: stage tap in a 3-stage synchroniser on the second (still intermediate) stage still triggers RDC-016.
// expect: RDC-016 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_n, d, output logic q, tap);
  logic r1, r2, r3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {r3, r2, r1} <= 3'b000; else {r3, r2, r1} <= {r2, r1, 1'b1};
  always_ff @(posedge clka or negedge r3) if (!r3) q <= 1'b0; else q <= d;
  assign tap = r2;
endmodule

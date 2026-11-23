// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Boundary: tapping the final stage (r2) of a 2-stage reset synchroniser is legal and must not trigger RDC-016.
// expect-none: RDC-016
module top(input logic clka, rst_n, d, output logic q, tap);
  logic r1, r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {r2, r1} <= 2'b00; else {r2, r1} <= {r1, 1'b1};
  always_ff @(posedge clka or negedge r2) if (!r2) q <= 1'b0; else q <= d;
  assign tap = r2;
endmodule

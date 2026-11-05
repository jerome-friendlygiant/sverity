// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The first stage only feeds the next synchronizer stage.
// expect-none: RDC-016
module top(input logic clka, rst_n, d, output logic q);
  logic r1, r2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {r2, r1} <= 2'b0; else {r2, r1} <= {r1, 1'b1};
  always_ff @(posedge clka or negedge r2) if (!r2) q <= 1'b0; else q <= d;
endmodule

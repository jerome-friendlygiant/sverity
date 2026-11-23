// sverity-args: --top top --sdc @FIXTURES@/CDC-003/excl.sdc
// Boundary: reset generated in a declared-exclusive clock domain emits RDC-007 INFO rather than ERROR.
// expect: RDC-007 INFO
// expect-none: RDC-007 ERROR
module top(input logic clka, clkb, arst_n, d, output logic q);
  logic ra, rb;
  always_ff @(posedge clka or negedge arst_n) if (!arst_n) {rb, ra} <= 2'b00; else {rb, ra} <= {ra, 1'b1};
  always_ff @(posedge clkb or negedge rb) if (!rb) q <= 1'b0; else q <= d;
endmodule

// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clkb or negedge rb) if (!rb) q => always_ff @(posedge clka or negedge rb) if (!rb) q
// revert-expect-none: RDC-007 ERROR
// The reset of the clkb registers is released by a register in the (declared asynchronous) clka domain.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, arst_n, d, output logic q);
  logic ra, rb;
  always_ff @(posedge clka or negedge arst_n) if (!arst_n) {rb, ra} <= 2'b00; else {rb, ra} <= {ra, 1'b1};
  always_ff @(posedge clkb or negedge rb) if (!rb) q <= 1'b0; else q <= d;
endmodule

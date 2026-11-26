// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign o = q & r; => assign o = q & q;
// revert-expect-none: RDC-011 WARNING
// One output combines registers of two reset sources: its value depends on which source releases first.
// expect: RDC-011 WARNING
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clka, rst_a_n, rst_b_n, d, output logic o);
  logic q, r;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= d;
  assign o = q & r;
endmodule

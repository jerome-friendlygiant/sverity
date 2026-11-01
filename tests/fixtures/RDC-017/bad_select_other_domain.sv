// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign r = rst_n | sel; => assign r = rst_n;
// revert-expect-none: RDC-017 WARNING
// The reset is muxed by a register of another reset domain.
// expect: RDC-017 WARNING
module top(input logic clka, rst_n, rst_other_n, d, sel_d, output logic q);
  logic sel, r;
  always_ff @(posedge clka or negedge rst_other_n) if (!rst_other_n) sel <= 1'b0; else sel <= sel_d;
  assign r = rst_n | sel;
  always_ff @(posedge clka or negedge r) if (!r) q <= 1'b0; else q <= d;
endmodule

// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign r = rst_n & ctl; => assign r = rst_n;
// revert-expect-none: RDC-007 ERROR
// A reset built by logic from a register of another (declared asynchronous) clock domain.
// expect: RDC-007 ERROR
module top(input logic clka, clkb, rst_n, d, e, output logic q);
  logic ctl, r;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) ctl <= 1'b0; else ctl <= d;
  assign r = rst_n & ctl;
  always_ff @(posedge clkb or negedge r) if (!r) q <= 1'b0; else q <= e;
endmodule

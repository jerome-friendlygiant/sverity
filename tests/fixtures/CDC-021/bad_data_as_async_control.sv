// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clkb or negedge ctl) if (!ctl) q => always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q
// revert-expect-none: CDC-021 ERROR
// A functional register output also acts as the asynchronous clear of a register in another clock domain.
// expect: CDC-021 ERROR
module top(input logic clka, clkb, rst_n, d, e, output logic q, y);
  logic ctl;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) ctl <= 1'b1; else ctl <= d;
  assign y = ctl & e;
  always_ff @(posedge clkb or negedge ctl) if (!ctl) q <= 1'b0; else q <= e;
endmodule

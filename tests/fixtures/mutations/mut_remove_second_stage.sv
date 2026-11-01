// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// MUTATION of CDC-003/good_two_flop.sv: the second flop is deleted.
// expect: CDC-003 ERROR
// expect-none: CDC-003 INFO
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, s1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= a_q;
  assign q = s1;
endmodule

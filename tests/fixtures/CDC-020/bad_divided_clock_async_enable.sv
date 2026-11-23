// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The toggling cdiv register is a structurally evidenced divided clock operand.
// Its source clock is clka; an enable launched by asynchronous clkb remains a hazard.
// expect: CDC-020 ERROR
module top(input logic clka, clkb, en_d, d, output logic q);
  logic cdiv, en_q, g;
  always_ff @(posedge clka) cdiv <= ~cdiv;
  always_ff @(posedge clkb) en_q <= en_d;
  assign g = cdiv & en_q;
  always_ff @(posedge g) q <= d;
endmodule

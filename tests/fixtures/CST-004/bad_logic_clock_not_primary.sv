// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// AUDIT t5: a child port that merely shares the name `clka` but is driven by a clock MUX is not the primary clka.
// The crossing must be unresolved (WARNING), never "same clock".
// expect: CDC-002 WARNING
// expect: CDC-003 WARNING
module sub(input logic clka, input logic d, output logic q);
  always @(posedge clka) q <= d;
endmodule
module top(input logic clka, clkb, sel, d, output logic q);
  logic a_q;
  always @(posedge clka) a_q <= d;
  sub u(.clka(sel ? clkb : clka), .d(a_q), .q(q));
endmodule

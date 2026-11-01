// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// AUDIT t2: `tmp = a_q; q <= tmp;` is ONE flop (tmp is combinational). It must not be recognised as a 2-stage synchroniser.
// expect: CDC-003 ERROR
// expect-none: CDC-003 INFO
module top(input logic clka, clkb, d, output logic q);
  logic a_q, tmp;
  always @(posedge clka) a_q <= d;
  always @(posedge clkb) begin tmp = a_q; q <= tmp; end
endmodule

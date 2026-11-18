// sverity-args: --top top
// One inversion of the clock is an inverter on it, not a copy: glitch-free, synthesis builds it as written (INFO).
// expect: LNT-047 INFO 8
module top(input logic in, d, output logic q);
  logic inv, out;
  assign inv = ~in;
  assign out = inv;
  always_ff @(posedge out) q <= d;
endmodule
// revert: assign inv = ~in; => assign inv = in;
// revert-expect-none: LNT-047

// sverity-args: --top top
// A weak driver loses to the strong one in simulation, but synthesis may ignore strengths and build two drivers.
// expect: LNT-009 WARNING 6
module top(input logic a, b, output wire w);
  assign (weak0, weak1) w = a;
  assign w = b;
endmodule

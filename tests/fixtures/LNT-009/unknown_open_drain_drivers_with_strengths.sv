// sverity-args: --top top
// Two open-drain drivers (highz1) of one wire: simulation resolves the wired AND by strength, synthesis may ignore the strengths, so Unresolved.
// expect: LNT-009 WARNING 6
module top(input logic a, b, output wire w);
  assign (strong0, highz1) w = a;
  assign (strong0, highz1) w = b;
endmodule

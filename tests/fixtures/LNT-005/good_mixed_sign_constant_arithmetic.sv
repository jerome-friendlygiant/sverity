// sverity-args: --top top
// A signed parameter that is not negative added to an unsigned vector of the same width: the constant converts unchanged.
// expect: LNT-005 INFO 7
// expect-none: LNT-005 WARNING
module top (input logic [31:0] u, output logic [31:0] y);
  localparam int Step = 5;
  assign y = u + Step;
endmodule

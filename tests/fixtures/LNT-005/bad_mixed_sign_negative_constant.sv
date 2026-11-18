// sverity-args: --top top
// A negative signed parameter added to an unsigned vector of the same width becomes a large unsigned number.
// revert: localparam int Step = -5 => localparam int Step = 5
// revert-expect-none: LNT-005 WARNING
// expect: LNT-005 WARNING 8
module top (input logic [31:0] u, output logic [31:0] y);
  localparam int Step = -5;
  assign y = u + Step;
endmodule

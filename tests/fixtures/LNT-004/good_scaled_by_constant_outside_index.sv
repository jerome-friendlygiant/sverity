// sverity-args: --top top
// A 16-bit value times a parameter, evaluated at 32 bits, stays inside them: no bit is lost whatever the context.
// expect: LNT-004 INFO 6
// expect-none: LNT-004 WARNING
module top #(parameter int Scale = 4)(input logic [15:0] w, output logic [31:0] y);
  assign y = w * Scale;
endmodule

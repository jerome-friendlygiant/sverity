// sverity-args: --top top
// Reduced from OpenTitan kmac: the target's own name appears only inside $bits, which is a compile-time constant.
// expect-none: LNT-012
module top(input logic [1:0] a, output logic [3:0] y);
  logic [3:0] d;
  assign d[1:0] = a;
  assign d[$bits(d) - 1:2] = '0;
  assign y = d;
endmodule

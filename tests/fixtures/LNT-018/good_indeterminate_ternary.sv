// sverity-args: --top top
// expect-none: LNT-018
module top(input logic [2:0] a, b, output logic [2:0] y);
 assign y = 1'bx ? a : b;
endmodule

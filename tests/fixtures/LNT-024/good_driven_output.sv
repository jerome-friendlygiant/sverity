// sverity-args: --top top
// expect-none: LNT-024
module top(input logic [3:0] a, output logic [3:0] y);
  assign y = a;
endmodule

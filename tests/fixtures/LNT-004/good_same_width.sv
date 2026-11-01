// sverity-args: --top top
// expect-none: LNT-004
module top(input logic [7:0] b, output logic [7:0] a);
  assign a = b;
endmodule

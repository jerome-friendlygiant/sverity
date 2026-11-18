// sverity-args: --top top
// A left shift beneath the modular difference loses its top bits before the compare.
// expect: LNT-036 WARNING 5
module top(input logic [7:0] a, c, output logic lt);
  assign lt = ((a << 2) - c) < 8'd9;
endmodule

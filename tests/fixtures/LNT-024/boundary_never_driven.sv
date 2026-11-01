// sverity-args: --top top
// expect: LNT-024 WARNING 3
module top(input logic a, output logic y);
  logic unused_in;
  assign unused_in = a;
endmodule

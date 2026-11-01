// sverity-args: --top top
// expect: LNT-008 WARNING
module top(input logic a, output logic y);
  wire w;
  assign y = w & a;
endmodule

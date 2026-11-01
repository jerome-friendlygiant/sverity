// sverity-args: --top top
// expect: LNT-011 WARNING
module top(input logic a, output logic y);
  logic spare;
  assign y = a;
endmodule

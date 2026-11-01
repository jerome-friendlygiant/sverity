// sverity-args: --top top
// expect: LNT-041 WARNING 4
module top(input logic a, input logic b, output logic y);
  logic m [3];
  assign m[0] = a;
  assign m[1] = b;
  assign y = m[0] & m[1];
endmodule

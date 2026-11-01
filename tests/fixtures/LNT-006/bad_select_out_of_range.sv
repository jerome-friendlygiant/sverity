// sverity-args: --top top
// expect: LNT-006 ERROR
module top(input logic [3:0] v, output logic b);
  assign b = v[7];
endmodule

// sverity-args: --top top
// expect: LNT-039 WARNING 5
// expect-count: LNT-039 WARNING 2
module child(input logic a, output logic y);
  assign y = a ? 'x : a;
endmodule
module top(input logic a, output logic [1:0] y);
  child u0(a, y[0]);
  child u1(a, y[1]);
endmodule

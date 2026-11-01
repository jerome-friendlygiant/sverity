// sverity-args: --top top
// expect: LNT-040 WARNING 5
// expect-count: LNT-040 WARNING 4
module child(input logic a, b, output logic m, n);
  assign m = (a === b);
  assign n = (a == 1'bx);
endmodule
module top(input logic a, b, output logic [1:0] m, n);
  child u0(a, b, m[0], n[0]);
  child u1(a, b, m[1], n[1]);
endmodule

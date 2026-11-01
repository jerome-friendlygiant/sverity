// sverity-args: --top top
// the child body is shared by two instances: one finding with count 2
// expect: LNT-062 WARNING 7
// expect-count: LNT-062 WARNING 2
module child(input logic a, output logic y);
  always_comb begin
    y <= a;
  end
endmodule
module top(input logic a, output logic y0, y1);
  child u0(a, y0);
  child u1(~a, y1);
endmodule

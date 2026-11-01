// sverity-args: --top top
// the child body is shared by two instances: one finding, count 2
// expect: LNT-053 WARNING 8
// expect-count: LNT-053 WARNING 2
module child(input logic a, b, output logic y);
  logic t;
  always_comb begin
    t = a;
    t = b;
    y = t;
  end
endmodule
module top(input logic a, b, output logic y0, y1);
  child u0(a, b, y0);
  child u1(b, a, y1);
endmodule

// sverity-args: --top top
// the child body is shared by two instances: one finding with count 2
// expect: LNT-044 WARNING 8
// expect-count: LNT-044 WARNING 2
module child(input logic a, output logic y, z);
  logic x;
  always_comb begin
    y = x;
    x = a;
    z = x;
  end
endmodule
module top(input logic a, output logic y0, z0, y1, z1);
  child u0(a, y0, z0);
  child u1(a, y1, z1);
endmodule

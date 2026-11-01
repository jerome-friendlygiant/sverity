// sverity-args: --top top
// the child body is shared by two instances: one finding with count 2
// expect: LNT-045 WARNING 6
// expect-count: LNT-045 WARNING 2
module child(input logic a, b, output logic y);
  always @(a) begin
    y = a & b;
  end
endmodule
module top(input logic a, b, output logic y0, y1);
  child u0(a, b, y0);
  child u1(b, a, y1);
endmodule

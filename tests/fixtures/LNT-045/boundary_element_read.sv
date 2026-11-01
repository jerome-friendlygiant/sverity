// sverity-args: --top top
// an element read counts as reading the whole array; listing one element still covers the array symbol
// expect-none: LNT-045
module top(input logic a, output logic y);
  logic [1:0] m;
  always @(m[0] or a) begin
    y = m[1] & a;
  end
endmodule

// sverity-args: --top top
// A left shift by a run-time count is LNT-035's; LNT-036 judges only a constant count and the own-operand compare (a << s) < a.
// expect-none: LNT-036
module top(input logic [7:0] m, t, input logic [2:0] s, output logic ge, nz, clr);
  assign ge = (m << s) >= t;
  assign nz = (m << s) != 8'd0;
  assign clr = ((m << s) >> s) == m;
endmodule

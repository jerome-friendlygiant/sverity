// sverity-args: --top top -Wundefined-param-override
// The instance overrides WIDTH, which sub does not declare; the option lowers slang's default error, it stays ERROR.
// expect: LNT-003 ERROR 9
// expect: LNT-001 FATAL
module sub #(parameter int W = 4) (input logic [W-1:0] a, output logic [W-1:0] y);
  assign y = a;
endmodule
module top(input logic [3:0] a, output logic [3:0] y);
  sub #(.WIDTH(4)) u(.a(a), .y(y));
endmodule

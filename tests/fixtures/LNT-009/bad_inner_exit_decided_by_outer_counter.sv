// sverity-args: --top top
// The inner break depends on the outer counter and runs only for i == 2: the other outer iterations write every v[j], including
// v[7], which the second process writes too.
// expect: LNT-009 ERROR 8
module top(input logic c, output logic [7:0] y);
  logic [7:0] v;
  always @(c) for (int i = 0; i < 4; i++) for (int j = 0; j < 8; j++) begin if (i == 2) break; v[j] = c; end
  always @(c) v[7] = 1'b0;
  assign y = v;
endmodule

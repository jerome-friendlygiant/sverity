// sverity-args: --top top
// The right operand is conditional on the left operand, so the loop does not prove this select always executes.
// expect-none: LNT-006
module top(input logic [3:0] d, output logic q);
  always_comb begin
    q = 1'b0;
    for (int i = 0; i <= 4; i++) if (q && d[i]) q = 1'b1;
  end
endmodule

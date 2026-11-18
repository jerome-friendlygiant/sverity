// sverity-args: --top top
// The left operand of && is always evaluated.
// expect: LNT-006 ERROR 7
module top(input logic [3:0] d, output logic q);
  always_comb begin
    q = 1'b0;
    for (int i = 0; i <= 4; i++) if (d[i] && q) q = 1'b1;
  end
endmodule

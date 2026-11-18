// sverity-args: --top top
// Both always procedures sit in the branch that elaborates: two drivers of err_o.
// expect: LNT-009 ERROR 8
module top(input logic [1:0] sel, output logic err_o);
  localparam int Num = 2;
  if (Num == 2) begin : g2
    always_comb err_o = sel[1];
    always_comb err_o = sel[0];
  end else begin : g4
    always_comb err_o = 1'b0;
  end
endmodule

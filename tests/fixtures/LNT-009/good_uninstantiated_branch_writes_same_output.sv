// sverity-args: --top top
// expect-none: LNT-009
// Each instance elaborates one branch of the generate if, so each output has one driver.
module chk #(parameter int Num = 2) (input logic [1:0] sel, output logic err_o);
  if (Num == 2) begin : g2
    always_comb err_o = sel[1];
  end else if (Num == 3) begin : g3
    always_comb err_o = &sel;
  end else begin : g4
    always_comb err_o = 1'b0;
  end
endmodule
module top(input logic [1:0] s, output logic e2, e4);
  chk #(.Num(2)) u2 (.sel(s), .err_o(e2));
  chk #(.Num(4)) u4 (.sel(s), .err_o(e4));
endmodule

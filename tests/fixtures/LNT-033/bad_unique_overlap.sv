// sverity-args: --top top
// expect: LNT-033 ERROR 8
module top(input logic [1:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    unique casez (s)
      2'b1?: y = 1'b1;
      2'b11: y = 1'b0;
      default: y = 1'b0;
    endcase
  end
endmodule

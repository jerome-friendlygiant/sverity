// sverity-args: --top top
// expect: LNT-033 ERROR 9
// The selector is the widest operand, so 2'b11 and 4'b0011 are the same value.
module top(input logic [3:0] s, output logic [1:0] y);
  always_comb begin
    y = 2'd0;
    unique case (s)
      2'b11: y = 2'd1;
      4'b0011: y = 2'd2;
      default: y = 2'd3;
    endcase
  end
endmodule

// sverity-args: --top top
// expect: LNT-065 WARNING
module top(input logic [1:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    case (s)
      2'd0: y = 1'b1;
      2'd1: y = 1'b0;
    endcase
  end
endmodule

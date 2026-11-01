// sverity-args: --top top
// expect: LNT-032 WARNING
module top(input logic [1:0] s, output logic y);
  always_comb begin
    case (s)
      2'd0: y = 1'b0;
      2'd0: y = 1'b1;
      default: y = 1'b0;
    endcase
  end
endmodule

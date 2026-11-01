// sverity-args: --top top
// expect: LNT-032 WARNING 8
module top(input logic [1:0] s, output logic y);
  always_comb begin
    y = 1'b0;
    casez (s)
      2'b??: y = 1'b1;
      2'b01: y = 1'b0;
      default: y = 1'b0;
    endcase
  end
endmodule

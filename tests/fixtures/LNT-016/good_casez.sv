// sverity-args: --top top
// expect-none: LNT-016
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    casez (s)
      2'b1?: y = 1;
      2'b01: y = 2;
      default: y = 0;
    endcase
  end
endmodule

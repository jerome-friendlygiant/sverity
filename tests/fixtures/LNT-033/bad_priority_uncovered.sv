// sverity-args: --top top
// expect: LNT-081 WARNING 6
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    y = 2'd0;
    priority case (s)
      2'd0: y = 2'd1;
      2'd1: y = 2'd2;
    endcase
  end
endmodule

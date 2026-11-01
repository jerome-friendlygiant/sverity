// sverity-args: --top top
// expect-none: LNT-033
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    priority case (s)
      2'd0: y = 2'd1;
      2'd1: y = 2'd2;
      default: y = 2'd0;
    endcase
  end
endmodule

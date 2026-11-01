// sverity-args: --top top
// expect: LNT-040 WARNING 7
module top(input logic [1:0] a, output logic m);
  always_comb begin
    m = 1'b0;
    case (a)
      2'bx0: m = 1'b1;
      default: m = 1'b0;
    endcase
  end
endmodule

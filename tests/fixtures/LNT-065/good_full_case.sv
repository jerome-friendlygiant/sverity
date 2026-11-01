// sverity-args: --top top
// every value is covered and a default is present
// expect-none: LNT-065
module top(input logic [1:0] s, output logic [1:0] y);
  always_comb begin
    case (s)
      2'd0: y = 1;
      2'd1: y = 2;
      2'd2: y = 3;
      default: y = 0;
    endcase
  end
endmodule

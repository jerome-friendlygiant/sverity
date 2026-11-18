// sverity-args: --top top
// A constant row index and a loop index select every element of a two-dimension packed array.
// expect-none: LNT-041
module top(input logic c, output logic [1:0][3:0] y);
  logic [1:0][3:0] k;
  always_comb begin
    for (int i = 0; i < 4; i++) begin
      k[0][i] = c;
      k[1][i] = ~c;
    end
  end
  assign y = k;
endmodule

// sverity-args: --top top
// A word written only under a condition keeps its value otherwise: the array is not plain wiring, and the latch is a WARNING of LNT-013.
// expect: LNT-013 WARNING 8
module top(input logic [7:0] a, b, input logic en, input logic [1:0] ra, output logic [7:0] z);
  logic [7:0] n [4];
  always_comb begin
    n[0] = a; n[1] = a; n[2] = a;
    if (en) n[3] = b;
  end
  assign z = n[ra];
endmodule

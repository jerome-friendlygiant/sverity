// sverity-args: --top top
// The local is written twice (once from an input): its value at the select is not what one assignment gives.
// expect-unresolved: LNT-006 11
module top(input logic [7:0] c, input int shift, output logic [7:0] d);
  localparam int W = 8;
  always_comb begin
    for (int z = 0; z < W; z++) begin
      int prev;
      prev = (z == 0) ? W - 1 : z - 1;
      if (z == 3) prev = shift;
      d[z] = c[prev];
    end
  end
endmodule

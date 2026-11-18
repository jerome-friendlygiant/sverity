// sverity-args: --top top
// The local `prev` is written once, before it is read, with a value in 0..W-1: the select stays inside the vector.
// expect-none: LNT-006
module top(input logic [7:0] c, output logic [7:0] d);
  localparam int W = 8;
  always_comb begin
    for (int z = 0; z < W; z++) begin
      int prev;
      prev = (z == 0) ? W - 1 : z - 1;
      d[z] = c[prev];
    end
  end
endmodule

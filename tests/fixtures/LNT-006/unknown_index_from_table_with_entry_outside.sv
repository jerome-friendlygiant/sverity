// sverity-args: --top top
// One entry of the table (5) is outside the 5-bit vector: the select can read outside it for the loop value that picks it.
// expect-unresolved: LNT-006 7
module top(input logic [4:0] c, output logic [4:0] d);
  localparam int Next [5] = '{4, 0, 1, 5, 3};
  always_comb begin
    for (int x = 0; x < 5; x++) d[x] = c[Next[x]];
  end
endmodule

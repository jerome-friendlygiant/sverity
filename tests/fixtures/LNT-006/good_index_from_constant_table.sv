// sverity-args: --top top
// The index is an element of a localparam table whose every entry is inside the array: the loop variable picks which entry, and each is 0..4.
// expect-none: LNT-006
module top(input logic [4:0] c, output logic [4:0] d);
  localparam int Next [5] = '{4, 0, 1, 2, 3};
  always_comb begin
    for (int x = 0; x < 5; x++) d[x] = c[Next[x]];
  end
endmodule

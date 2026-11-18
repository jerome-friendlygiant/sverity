// sverity-args: --top top
// A break can end the loop before the index leaves the range, so no iteration is proven to run the select.
// expect-none: LNT-006
module top(input logic [7:0] d, output logic [3:0] q);
  always_comb begin
    q = '0;
    for (int i = 0; i < 8; i++) begin
      if (i == 4) break;
      q[i] = d[i];
    end
  end
endmodule

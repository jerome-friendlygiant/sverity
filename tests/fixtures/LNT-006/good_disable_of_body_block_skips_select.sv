// sverity-args: --top top
// `disable body` ends the iteration before the select when x[i] is set, so the select is not run on every iteration.
// expect-none: LNT-006
module top(input logic [15:0] x, input logic [7:0] a, output logic y);
  always_comb begin
    y = 0;
    for (int i = 0; i < 16; i++) begin : body
      if (x[i]) disable body;
      y = y ^ a[i];
    end
  end
endmodule

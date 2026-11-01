// sverity-args: --top top
// a write in a loop body is not overwritten by the next iteration unconditionally; no dead-store claim
// expect-none: LNT-053
module top(input logic [1:0] c, input logic [1:0] a, output logic y);
  always_comb begin
    y = 1'b0;
    for (int i = 0; i < 2; i++) if (c[i]) y = a[i];
  end
endmodule

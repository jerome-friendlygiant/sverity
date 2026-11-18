// sverity-args: --top top
// expect-none: LNT-008
module top(input logic [7:0] a, output logic [7:0] y);
  logic [7:0] v;
  always_comb begin
    for (int i = 0; i < 8; i++) v[i] = a[i];
    y = v;
  end
endmodule

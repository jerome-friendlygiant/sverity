// sverity-args: --top top
// expect: LNT-008 WARNING 4
module top(input logic en, input logic [7:0] a, output logic [7:0] y);
  logic [7:0] v;
  always_comb begin
    for (int i = 0; i < 7; i++) if (en) v[i] = a[i];
    y = v;
  end
endmodule

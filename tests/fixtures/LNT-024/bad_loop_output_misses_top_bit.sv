// sverity-args: --top top
// expect: LNT-024 WARNING 5
// revert: i < 7 => i < 8
// revert-expect-none: LNT-024
module top(input logic [7:0] a, output logic [7:0] y);
  always_comb begin
    for (int i = 0; i < 7; i++) y[i] = a[i];
  end
endmodule

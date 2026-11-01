// sverity-args: --top top
// if (b) x = 0 comes after if (a) x = 1: when both hold the later assignment wins
// expect: LNT-025 WARNING 9
module top(input logic a, b, output logic y);
  logic x;
  always_comb begin
    x = 1'b0;
    if (a) x = 1'b1;
    if (b) x = 1'b0;
    y = x;
  end
endmodule

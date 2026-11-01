// sverity-args: --top top
// the conditions cannot both hold (same selector, different constants; x and !x): no finding
// expect-none: LNT-025
module top(input logic [1:0] sel, input logic en, a, b, output logic y);
  logic x, z;
  always_comb begin
    x = a;
    if (sel == 2'd0) x = b;
    if (sel == 2'd1) x = ~b;
    z = a;
    if (!en) z = b;
    if (en) z = ~b;
    y = x ^ z;
  end
endmodule

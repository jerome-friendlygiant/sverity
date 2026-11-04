// sverity-args: --top top
// Identical variable expressions need not evaluate equally at different writes.
// expect: LNT-025 WARNING
module top(input logic a, b, d, output logic q);
  logic v;
  always_comb begin
    v = d;
    q = 0;
    if (a) q = v;
    v = !v;
    if (b) q = v;
  end
endmodule

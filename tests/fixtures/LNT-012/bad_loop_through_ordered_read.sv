// sverity-args: --top top
// The procedure reads `a` only after writing it, yet the loop is real: c -> a -> b -> c. Order must not hide it.
// expect: LNT-012 ERROR
module top(input logic d, output logic y);
  logic a, b, c;
  always_comb begin
    a = c ^ d;
    b = a;
  end
  assign c = b;
  assign y = b;
endmodule

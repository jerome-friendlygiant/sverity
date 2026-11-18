// sverity-args: --top top
// The write comes before the read on every path, so the read sees this activation's own value: no loop.
// expect-none: LNT-012
module top(input logic x, z, output logic y);
  logic a;
  always_comb begin
    a = z;
    a = a ^ x;
  end
  assign y = a;
endmodule

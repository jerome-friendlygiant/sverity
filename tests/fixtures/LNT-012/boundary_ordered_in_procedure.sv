// sverity-args: --top top
// A variable read after it is assigned in one always_comb is not a loop: Flow proves the order, so no finding.
// expect-none: LNT-012
module top(input logic a, output logic y);
  always_comb begin
    y = a;
    y = y & a;
  end
endmodule

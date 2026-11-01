// sverity-args: --top top
// A variable read after it is assigned in one always_comb is not a loop: reported only as a heuristic WARNING at most.
// expect-none: LNT-012 ERROR
module top(input logic a, output logic y);
  always_comb begin
    y = a;
    y = y & a;
  end
endmodule

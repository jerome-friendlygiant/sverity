// sverity-args: --top top
// expect: LNT-018 WARNING 6
module top(input logic a, output logic y);
  always_comb begin
    y = a;
    if (2 > 3) y = ~a;
  end
endmodule

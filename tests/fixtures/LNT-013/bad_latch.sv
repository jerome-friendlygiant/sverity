// sverity-args: --top top
// expect: LNT-013 WARNING 5
module top(input logic en, input logic d, output logic q);
  always_comb begin
    if (en) q = d;
  end
endmodule

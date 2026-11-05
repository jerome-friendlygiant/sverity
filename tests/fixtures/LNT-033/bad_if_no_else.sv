// sverity-args: --top top
// expect: LNT-081 WARNING 6
module top(input logic a, input logic b, output logic y);
  always_comb begin
    y = 1'b0;
    unique if (a) y = 1'b1;
    else if (b) y = 1'b0;
  end
endmodule

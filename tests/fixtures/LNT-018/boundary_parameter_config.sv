// sverity-args: --top top
// a condition that depends on a parameter is configuration dependent: Info, not Warning
// expect: LNT-018 INFO 7
module top #(parameter bit DEBUG = 1'b0) (input logic a, output logic y);
  always_comb begin
    y = a;
    if (DEBUG) y = ~a;
  end
endmodule

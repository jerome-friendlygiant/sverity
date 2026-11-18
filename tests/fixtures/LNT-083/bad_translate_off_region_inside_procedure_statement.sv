// sverity-args: --top top
// The region starts and ends inside an always_comb: simulation runs the overriding assignment, synthesis builds the block without it.
// expect: LNT-083 WARNING 7
module top(input logic a, b, output logic y);
  always_comb begin
    y = a;
    // synopsys translate_off
    if (b) y = 1'b0;
    // synopsys translate_on
  end
endmodule

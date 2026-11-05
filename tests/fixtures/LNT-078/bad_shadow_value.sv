// sverity-args: --top top
// expect: LNT-078 WARNING
module top(input logic a, output logic y);
  logic value;
  always_comb begin : block
    logic value;
    value = a;
    y = value;
  end
endmodule

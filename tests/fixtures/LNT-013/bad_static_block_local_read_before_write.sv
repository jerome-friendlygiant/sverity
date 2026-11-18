// sverity-args: --top top
// u is static: y sees the value u kept from the previous activation, so the block holds state.
// expect: LNT-013 WARNING 7
module top(input logic [3:0] a, output logic [3:0] y);
  always_comb begin : b
    logic [3:0] u;
    y = u;
    u = a;
  end
endmodule

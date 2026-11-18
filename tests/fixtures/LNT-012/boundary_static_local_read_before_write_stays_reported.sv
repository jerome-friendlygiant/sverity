// sverity-args: --top top
// A static block local that is read before it is written holds its value between activations: it is not a loop counter, and the
// self-dependency is reported as for any variable.
// expect: LNT-012 WARNING 6
module top(input logic [3:0] a, output logic [3:0] y);
  always_comb begin : b1
    logic [3:0] t;
    t = t + a;
    y = t;
  end
endmodule
